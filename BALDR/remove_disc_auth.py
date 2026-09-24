#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
VisualArtsDiscAuthTools —— 旧式 comap.dat + reg.exe 光盘认证移除工具

用途
----
针对 2000 年代 VisualArt's 系（无壳）Windows 游戏 EXE，移除启动时的旧式光盘
认证。这类认证会调用系统自带的 reg.exe，而旧参数格式在 Win10/11 上已经失效，
导致游戏弹出「请放入原版光盘」之类的提示并无法启动。

本工具不删除认证代码、不修改游戏主体逻辑，只把「认证包装函数」的入口改成：

    55 8B EC ...      ->   B8 01 00 00 00 C3
    (原函数序言)          (mov eax,1 ; ret)

即让认证函数直接返回“成功（1）”。与已验证有效的 BaldrForceSE 补丁完全同一做法。

原理
----
认证包装函数的结构（VisualArt's 同期通用）：

    call  comap.dat 校验函数
    cmp   eax, 1
    jne   失败路径
    ret                  ; 成功直接返回
失败路径:
    push  1
    push  offset "reg.exe"
    call  [WinExec]
    ...   Sleep(1000) 循环最多 10 次重新校验 ...
    返回 0 / 1

只要把该函数入口改为 mov eax,1 ; ret，上层就会一直认为认证成功。

定位策略（通用）
----------------
1. 校验目标是否含明文 "comap.dat" + "reg.exe"，并导入 WinExec（带壳程序会被拒绝）。
2. 找到唯一引用 "reg.exe" 的 push 指令。
3. 从该点向前回溯到函数入口：找最近的「>=5 字节 0xCC 填充串结尾」，
   再用 capstone 线性反汇编确认从候选入口能走到该 push 点。
   （>=5 是为了绕开编译器 MOV EAX,0CCCCCCCCh 产生的 4 字节 CC。）
4. 校验该函数内确有 call [WinExec] 与 call [Sleep]。
5. 对已知文件（内置 SHA-256）直接使用校验过的固定偏移。

用法
----
    python remove_disc_auth.py BBR.exe
    python remove_disc_auth.py BaldrForceSE.exe
    python remove_disc_auth.py 某同期游戏.exe --dry-run      # 只检测不修改
    python remove_disc_auth.py BBR.exe --out BBR_new.exe     # 输出到新文件

安全策略
--------
- 定位不到唯一认证函数时拒绝修改，绝不乱改。
- 修改前自动备份为 <文件名>.bak（除非已存在或指定 --no-backup）。
- 修改后重新反汇编确认入口已是 mov eax,1 ; ret。
"""

import argparse
import hashlib
import shutil
import struct
import sys
from pathlib import Path

# ----------------------------------------------------------------------------
# 可选依赖：capstone（用于反汇编校验）。缺失时退化为仅特征定位。
# ----------------------------------------------------------------------------
try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
except Exception:  # pragma: no cover
    Cs = None

NEW_BYTES = bytes.fromhex("B8 01 00 00 00 C3")  # mov eax,1 ; ret
MIN_CC_RUN = 5  # 判定“函数间填充”的最小 0xCC 串长度

# ----------------------------------------------------------------------------
# 已分析并验证过的文件（SHA-256 -> 补丁信息）
# ----------------------------------------------------------------------------
KNOWN_FILES = {
    # BBR.exe (BALDR BULLET REBELLION, 2006)
    "e7bfefeb3479c44d4ba09054575ca8d033d925b2a4d6ed7e02d7fe98228613be": {
        "label": "BBR.exe",
        "offset": 0x136E20,
        "old": bytes.fromhex("55 8B EC 83 EC 44"),
    },
    # BaldrForceSE.exe (BALDR FORCE Standard Edition, 2007)
    "afb81b11704cf22bc572ce39b48b1cd046dca1a47ca220b5df77fd02f9b6a5d8": {
        "label": "BaldrForceSE.exe",
        "offset": 0x001440,
        "old": bytes.fromhex("E8 4B FF FF FF 83"),
    },
}


# ----------------------------------------------------------------------------
# PE 解析（不依赖 pefile）
# ----------------------------------------------------------------------------
class PE:
    def __init__(self, data: bytes):
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("不是有效的 MZ/PE 文件")
        self.e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if data[self.e_lfanew:self.e_lfanew + 4] != b"PE\x00\x00":
            raise ValueError("不是有效的 PE 文件")
        fh = self.e_lfanew + 4
        (self.machine, self.nsections, _ts, _ps, _ns,
         self.optsize, self.characteristics) = struct.unpack_from("<HHIIIHH", data, fh)
        opt = fh + 20
        self.opt = opt
        self.magic = struct.unpack_from("<H", data, opt)[0]
        if self.magic != 0x10B:
            raise ValueError("目前只支持 32 位 PE (PE32)")
        self.imagebase = struct.unpack_from("<I", data, opt + 28)[0]
        self.entry_rva = struct.unpack_from("<I", data, opt + 16)[0]
        self.sections = []
        sec = opt + self.optsize
        for i in range(self.nsections):
            o = sec + i * 40
            name = data[o:o + 8].rstrip(b"\x00").decode(errors="ignore")
            vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
            self.sections.append({
                "name": name, "vaddr": vaddr, "vsize": vsize,
                "rawptr": rawptr, "rawsize": rawsize,
            })

    def rva2off(self, rva: int):
        for s in self.sections:
            size = max(s["vsize"], s["rawsize"])
            if s["vaddr"] <= rva < s["vaddr"] + size:
                return s["rawptr"] + (rva - s["vaddr"])
        return None

    def off2rva(self, off: int):
        for s in self.sections:
            if s["rawptr"] <= off < s["rawptr"] + s["rawsize"]:
                return s["vaddr"] + (off - s["rawptr"])
        return None

    def off2va(self, off: int):
        rva = self.off2rva(off)
        return None if rva is None else self.imagebase + rva

    def va2off(self, va: int):
        return self.rva2off(va - self.imagebase)

    def text_range(self):
        """返回 .text 段的 (raw 起, raw 止)。找不到则退化为整个文件。"""
        for s in self.sections:
            if s["name"] == ".text":
                return s["rawptr"], s["rawptr"] + s["rawsize"]
        return 0, len(self.data)

    def imports(self):
        """返回 {函数名: IAT 的 VA}。"""
        result = {}
        if self.optsize < 96 + 2 * 8:
            return result
        import_rva, _import_size = struct.unpack_from("<II", self.data, self.opt + 96 + 1 * 8)
        if not import_rva:
            return result
        off = self.rva2off(import_rva)
        if off is None:
            return result
        while True:
            oft, _ts, _fc, name_rva, first_thunk = struct.unpack_from("<IIIII", self.data, off)
            if oft == 0 and name_rva == 0 and first_thunk == 0:
                break
            dll_off = self.rva2off(name_rva)
            dll = ""
            if dll_off is not None:
                end = self.data.index(b"\x00", dll_off)
                dll = self.data[dll_off:end].decode(errors="ignore")
            thunk_rva = oft or first_thunk
            idx = 0
            while True:
                t_off = self.rva2off(thunk_rva + idx * 4)
                if t_off is None:
                    break
                thunk = struct.unpack_from("<I", self.data, t_off)[0]
                if thunk == 0:
                    break
                if not (thunk & 0x80000000):
                    n_off = self.rva2off(thunk)
                    if n_off is not None:
                        end = self.data.index(b"\x00", n_off + 2)
                        fname = self.data[n_off + 2:end].decode(errors="ignore")
                        result.setdefault(fname, self.imagebase + first_thunk + idx * 4)
                idx += 1
            off += 20
        return result


# ----------------------------------------------------------------------------
# 反汇编辅助
# ----------------------------------------------------------------------------
def _md():
    if Cs is None:
        return None
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    return md


def disasm_from(data, pe, start_off, length):
    md = _md()
    if md is None:
        return None
    return list(md.disasm(data[start_off:start_off + length], pe.imagebase + pe.off2rva(start_off)))


def reaches_offset(data, pe, start_off, target_off):
    """线性反汇编 start_off，判断是否有指令的起始地址正好落在 target_off。
    不因中途出现 ret/jmp 而提前中止（有些包装函数带提前 ret 的分支目标）。"""
    md = _md()
    if md is None:
        return True  # 没有 capstone 时不做此项校验
    stop = min(len(data), target_off + 8)
    for ins in md.disasm(data[start_off:stop], pe.imagebase + pe.off2rva(start_off)):
        off = pe.va2off(ins.address)
        if off == target_off:
            return True
        if off is not None and off > target_off:
            return False
    return False


def next_function_offset(data, start_off, limit=0x4000):
    """从 start_off 起，找到下一处 >=MIN_CC_RUN 的 0xCC 填充串起点，作为函数区终点。"""
    i = start_off + 1
    end = min(len(data), start_off + limit)
    while i < end:
        if data[i] == 0xCC:
            j = i
            while j + 1 < end and data[j + 1] == 0xCC:
                j += 1
            if (j - i + 1) >= MIN_CC_RUN:
                return i
            i = j + 1
        else:
            i += 1
    return end


# ----------------------------------------------------------------------------
# 定位认证包装函数
# ----------------------------------------------------------------------------
def find_push_site(data, pe, string_va):
    """找到唯一 push <string_va>（6A/68 形式优先），返回文件偏移。"""
    tstart, tend = pe.text_range()
    pat = b"\x68" + struct.pack("<I", string_va)
    hits = []
    i = tstart
    while True:
        p = data.find(pat, i, tend)
        if p < 0:
            break
        hits.append(p)
        i = p + 1
    if len(hits) == 1:
        return hits[0], "push imm32"
    if len(hits) > 1:
        return None, "push 引用不唯一: %s" % [hex(h) for h in hits]
    # 退化：任何 4 字节小端引用
    pat2 = struct.pack("<I", string_va)
    hits2 = []
    i = tstart
    while True:
        p = data.find(pat2, i, tend)
        if p < 0:
            break
        hits2.append(p)
        i = p + 1
    if len(hits2) == 1:
        return hits2[0], "imm32 引用"
    return None, "找不到唯一引用: %s" % [hex(h) for h in hits2]


def find_function_entry(data, pe, push_off):
    """从 push 点向前回溯函数入口（通用算法）。"""
    i = push_off - 1
    while i > 0:
        if data[i] != 0xCC:
            i -= 1
            continue
        j = i
        while j > 0 and data[j - 1] == 0xCC:
            j -= 1
        run_len = i - j + 1
        candidate = i + 1
        if run_len >= MIN_CC_RUN and candidate < push_off:
            if reaches_offset(data, pe, candidate, push_off):
                return candidate, "CC 填充(长度 %d)" % run_len
        i = j - 1
    # 退化：最近一个 ret 之后（跳过 CC）
    i = push_off - 1
    while i > 0:
        if data[i] in (0xC3, 0xCB):
            cand = i + 1
            while cand < push_off and data[cand] == 0xCC:
                cand += 1
            return cand, "ret 边界(退化)"
        i -= 1
    return None, "无法回溯函数入口"


def validate_wrapper(data, pe, entry_off, push_off, winexec_va, sleep_va):
    """校验该函数确实是 comap/reg.exe 认证包装函数。返回 (ok, notes)。"""
    notes = []
    end = next_function_offset(data, entry_off)
    region = data[entry_off:end]
    ok = True

    if winexec_va is not None:
        pat = b"\xff\x15" + struct.pack("<I", winexec_va)
        if pat in region:
            notes.append("含 call [WinExec]")
        else:
            ok = False
            notes.append("未找到 call [WinExec]")
    else:
        notes.append("导入表未找到 WinExec")

    if sleep_va is not None:
        pat = b"\xff\x15" + struct.pack("<I", sleep_va)
        notes.append("含 call [Sleep]" if pat in region else "警告：未找到 call [Sleep]")
    else:
        notes.append("导入表未找到 Sleep")

    if b"\x83\xf8\x01" in region or b"\x3d\x01\x00\x00\x00" in region:
        notes.append("含 cmp eax,1")
    else:
        notes.append("警告：未找到 cmp eax,1")

    if not (entry_off <= push_off < end):
        ok = False
        notes.append("push 点不在函数区内")
    return ok, notes


def locate_patch(data, pe, imports):
    """返回 (offset, old_bytes, info_str) 或抛出 RuntimeError。"""
    off_reg = data.find(b"reg.exe")
    off_comap = data.find(b"comap.dat")
    if off_reg < 0:
        raise RuntimeError("未发现明文 'reg.exe'（可能是带壳/其它认证，本工具不支持）")
    if off_comap < 0:
        raise RuntimeError("未发现明文 'comap.dat'（不是本工具针对的认证类型）")
    if data.find(b"reg.exe", off_reg + 1) >= 0:
        raise RuntimeError("'reg.exe' 出现多次，无法安全定位")

    reg_va = pe.off2va(off_reg)
    if reg_va is None:
        raise RuntimeError("reg.exe 字符串不在任何节内")
    push_off, how = find_push_site(data, pe, reg_va)
    if push_off is None:
        raise RuntimeError("定位 push reg.exe 失败：%s" % how)

    entry_off, boundary = find_function_entry(data, pe, push_off)
    if entry_off is None:
        raise RuntimeError("回溯认证函数入口失败：%s" % boundary)

    ok, notes = validate_wrapper(data, pe, entry_off, push_off,
                                 imports.get("WinExec"), imports.get("Sleep"))
    info = "入口=0x%X(%s) push=0x%X %s" % (entry_off, boundary, push_off, "；".join(notes))
    if not ok:
        raise RuntimeError("函数校验未通过：%s" % info)
    return entry_off, data[entry_off:entry_off + len(NEW_BYTES)], info


# ----------------------------------------------------------------------------
# 主流程
# ----------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(
        description="移除旧式 comap.dat + reg.exe 光盘认证（VisualArt's 系无壳 EXE）")
    ap.add_argument("exe", help="目标 EXE")
    ap.add_argument("--out", help="输出到新文件（默认原地修改）")
    ap.add_argument("--no-backup", action="store_true", help="不生成 .bak 备份")
    ap.add_argument("--dry-run", action="store_true", help="只检测并打印，不修改文件")
    args = ap.parse_args()

    src = Path(args.exe)
    if not src.is_file():
        print("[错误] 文件不存在：%s" % src)
        return 1

    data = src.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    print("[文件] %s" % src)
    print("[大小] %d 字节" % len(data))
    print("[SHA256] %s" % sha)

    try:
        pe = PE(data)
    except Exception as e:
        print("[错误] PE 解析失败：%s" % e)
        return 1
    print("[PE] ImageBase=0x%X Entry=0x%X 节数=%d" %
          (pe.imagebase, pe.imagebase + pe.entry_rva, pe.nsections))

    imports = pe.imports()

    # 1) 已知文件快速通道
    known = KNOWN_FILES.get(sha)
    if known:
        entry_off = known["offset"]
        old = known["old"]
        info = "已知文件 %s，使用校验过的固定偏移" % known["label"]
        use_known = True
    else:
        try:
            entry_off, old, info = locate_patch(data, pe, imports)
        except RuntimeError as e:
            print("[拒绝] %s" % e)
            return 1
        use_known = False

    print("[定位] %s" % info)
    print("[偏移] 0x%X" % entry_off)

    # 2) 状态 / 原字节校验
    cur6 = data[entry_off:entry_off + len(NEW_BYTES)]
    if cur6 == NEW_BYTES:
        print("[跳过] 该位置已经是 mov eax,1 ; ret，无需重复打补丁。")
        return 0
    if len(cur6) < len(NEW_BYTES):
        print("[拒绝] 目标偏移越界。")
        return 1
    print("[原字节] %s" % cur6.hex(" ").upper())
    if use_known and cur6 != old:
        print("[拒绝] 已知文件的原字节不匹配，文件可能已被修改。")
        return 1
    if not use_known:
        # 通用模式：要求入口是合法指令边界
        if _md() is not None:
            ins = disasm_from(data, pe, entry_off, 16)
            if not ins:
                print("[拒绝] 无法从目标偏移反汇编，放弃修改。")
                return 1

    if args.dry_run:
        print("[dry-run] 校验通过，将写入：%s" % NEW_BYTES.hex(" ").upper())
        print("[dry-run] 未修改任何文件。")
        return 0

    # 3) 备份
    out_path = Path(args.out) if args.out else src
    if out_path == src and not args.no_backup:
        backup = src.with_suffix(src.suffix + ".bak")
        if not backup.exists():
            shutil.copy2(src, backup)
            print("[备份] %s" % backup)
        else:
            print("[备份] 已存在，保留原备份：%s" % backup)

    # 4) 写补丁
    buf = bytearray(data)
    buf[entry_off:entry_off + len(NEW_BYTES)] = NEW_BYTES
    out_path.write_bytes(buf)
    print("[写入] %s 偏移 0x%X -> %s" % (out_path, entry_off, NEW_BYTES.hex(" ").upper()))

    # 5) 复核
    check = out_path.read_bytes()
    if check[entry_off:entry_off + len(NEW_BYTES)] != NEW_BYTES:
        print("[错误] 写入复核失败！")
        return 1
    if _md() is not None:
        ins = disasm_from(check, pe, entry_off, 8)
        text = " ; ".join("%s %s" % (i.mnemonic, i.op_str) for i in ins[:2]) if ins else "(反汇编失败)"
        print("[复核] 0x%X: %s" % (entry_off, text))
    print("[完成] 已移除旧式认证。原文件%s。" %
          ("已备份" if (out_path == src and not args.no_backup) else "未改动(输出到新文件)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
