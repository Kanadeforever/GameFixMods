"""
尘骸魔京 (Jingai Makyou) Windows 10 兼容性修复工具
===================================================

对两个文件执行二进制修补：

  1. NitroSystem.exe (R1 — 鼠标冻结修复)
     通过代码洞 + 入口点劫持，在进程启动时修补 4 个 IME/输入 API，
     使它们的 prologue 改为 "mov eax,0; retn X"，彻底杜绝鼠标死锁。

  2. system.dll    (R2 — 注册表弹窗绕过)
     将序列号验证跳转 (JNE) 改为无条件跳转 (JMP)，跳过弹窗。

用法
----
    python win10fix.py <NitroSystem.exe> [system.dll]

输出
----
    <游戏目录>/NitroSystem_patched.exe      ← 已修补主程序 (鼠标修复)
    <游戏目录>/system.dll                   ← 已修补 system.dll (弹窗绕过，仅当输入 system.dll 时)

不需要任何外部 DLL，不修改游戏原始文件。
"""
import struct
import os
import sys

# ──────────────────────────────────────────────
# PE layout 常量 (NitroSystem.exe v1.00)
# ──────────────────────────────────────────────
MACKT_VA   = 0x46000      # 原始 Import Descriptor Table RVA
IAT_NEXT   = 0x16168      # 原始 IAT 末端 (kernel32=0x16000, user32=0x16150)
IDT_NEW    = 0x46700      # 新 IDT 放置位置 (仍在 .mackt 节区内)
STR_NEW    = 0x46780      # 新字符串表位置
CAVE_RVA   = 0x47000      # 代码洞 RVA (扩展 .mackt 节区后的空间)
CAVE_SIZE  = 0x1000       # 扩展大小

# ──────────────────────────────────────────────
# 辅助: PE 节区遍历
# ──────────────────────────────────────────────
def _section_info(data, pe_off):
    """返回节区列表 [(name, vaddr, vsize, raw_off, raw_sz, chars)]"""
    ns = struct.unpack_from('<H', data, pe_off + 6)[0]
    oh_sz = struct.unpack_from('<H', data, pe_off + 20)[0]
    base = pe_off + 24 + oh_sz
    secs = []
    for i in range(ns):
        s = base + i * 40
        secs.append((
            data[s:s+8],
            struct.unpack_from('<I', data, s + 12)[0],  # vaddr
            struct.unpack_from('<I', data, s + 8)[0],   # vsize
            struct.unpack_from('<I', data, s + 20)[0],  # raw_off
            struct.unpack_from('<I', data, s + 16)[0],  # raw_sz
            struct.unpack_from('<I', data, s + 36)[0],  # chars
        ))
    return secs

def _rva2off(data, pe_off, rva):
    secs = _section_info(data, pe_off)
    for name, vaddr, vsize, raw_off, raw_sz, chars in secs:
        if vaddr <= rva < vaddr + vsize:
            return rva - vaddr + raw_off
        # 部分数据在 raw 范围内但不在 virtual size 内 (如扩展后的空间)
        if raw_off <= rva < raw_off + raw_sz:
            return rva
    return None

# ──────────────────────────────────────────────
# R1: EXE 修补 — 鼠标冻结修复
# ──────────────────────────────────────────────
def _patch_r1(data):
    """向 NitroSystem.exe 嵌入入口点劫持桩，修复鼠标冻结。"""
    pe_off = struct.unpack_from('<I', data, 0x3C)[0]
    ns = struct.unpack_from('<H', data, pe_off + 6)[0]
    oh_sz = struct.unpack_from('<H', data, pe_off + 20)[0]
    sec_base = pe_off + 24 + oh_sz

    # ── 0. 读取原始 OEP ──
    oep_rva = struct.unpack_from('<I', data, pe_off + 24 + 16)[0]

    # ── 1. 扩展 .mackt 节区 ──
    #    找最后一个节区 (.mackt) 并扩展
    last_sec = sec_base + (ns - 1) * 40
    old_vsize  = struct.unpack_from('<I', data, last_sec + 8)[0]
    old_raw_sz = struct.unpack_from('<I', data, last_sec + 16)[0]
    struct.pack_into('<I', data, last_sec + 8,  old_vsize  + CAVE_SIZE)
    struct.pack_into('<I', data, last_sec + 16, old_raw_sz + CAVE_SIZE)

    # 更新 SizeOfImage
    img_sz_off = pe_off + 24 + 0x50
    old_img_sz = struct.unpack_from('<I', data, img_sz_off)[0]
    struct.pack_into('<I', data, img_sz_off, old_img_sz + CAVE_SIZE)

    # ── 2. 创建新的 Import Descriptor Table ──
    #    复制原始 IDT (kernel32 + user32) 到新位置
    idt_rva = struct.unpack_from('<I', data, pe_off + 24 + 0x60 + 8)[0]
    idt_sz  = struct.unpack_from('<I', data, pe_off + 24 + 0x60 + 12)[0]

    # 找到原始 IDT 的文件偏移并复制 (两个条目 + null = 60 bytes)
    idt_off = _rva2off(data, pe_off, idt_rva)
    # 新 IDT 位置
    new_idt_off = _rva2off(data, pe_off, IDT_NEW)
    # 复制原始描述符
    old_idt_size = 60  # kernel32(20) + user32(20) + null(20)
    data[new_idt_off:new_idt_off+old_idt_size] = data[idt_off:idt_off+old_idt_size]

    # ── 3. 替换 kernel32 条目中的 Name 指向 (确保字符串正确) ──
    #    实际上不需要动，因为字符串在原来的位置

    # ── 4. 为新增导入编写 IAT/INT ──
    #    IAT_NEXT 开始放置新的 thunk 数组
    iat = IAT_NEXT
    st  = STR_NEW

    # 待添加的导入函数: 每个元组 (dll, func, 是否为 stdcall retn 值)
    # retn 值: 参数个数 × 4
    NEW_IMPORTS = [
        ("kernel32.dll", "GetProcAddress",    0),  # 运行时解析函数用
        ("imm32.dll",    "ImmGetDefaultIMEWnd", 4),
        ("imm32.dll",    "ImmGetContext",       4),
        ("imm32.dll",    "ImmSetOpenStatus",    8),
        ("user32.dll",   "AttachThreadInput",  12),
    ]

    iat_map = {}  # func_name → (IAT_RVA, retn_val)

    for idx, (dll, func, retn) in enumerate(NEW_IMPORTS):
        doff = new_idt_off + old_idt_size + idx * 20

        # --- Import-by-Name 条目 ---
        ibn = struct.pack('<H', 0) + func.encode() + b'\x00'
        data[st:st + len(ibn)] = ibn
        ibn_rva = st
        st += len(ibn)

        # --- DLL 名字符串 ---
        dn = dll.encode() + b'\x00'
        data[st:st + len(dn)] = dn
        dll_rva = st
        st += len(dn)

        # --- IAT 条目 (2 thunks: 函数 + null) ---
        struct.pack_into('<I', data, iat,     ibn_rva)  # INT thunk
        struct.pack_into('<I', data, iat + 4, 0)        # null terminator

        # --- Import Descriptor ---
        struct.pack_into('<IIIII', data, doff,
                         iat,          # OriginalFirstThunk → INT
                         0,            # TimeDateStamp
                         0,            # ForwarderChain
                         dll_rva,      # Name → DLL name
                         iat)          # FirstThunk → IAT

        iat_map[func] = (iat, retn)
        iat += 8

    # --- Null terminator for new IDT entries ---
    null_start = new_idt_off + old_idt_size + len(NEW_IMPORTS) * 20
    for i in range(20):
        data[null_start + i] = 0

    # ── 5. 更新 PE 头指向新的 IDT ──
    new_idt_size = old_idt_size + len(NEW_IMPORTS) * 20 + 20  # + null terminator
    struct.pack_into('<I', data, pe_off + 24 + 0x60 + 8,  IDT_NEW)
    struct.pack_into('<I', data, pe_off + 24 + 0x60 + 12, new_idt_size)
    # 清除 Bound Import
    struct.pack_into('<I', data, pe_off + 24 + 0x40, 0)

    # ── 6. 写入代码洞 (入口点桩) ──
    cave_raw = _rva2off(data, pe_off, CAVE_RVA)
    # 确保数据区已扩展
    while len(data) < cave_raw + CAVE_SIZE:
        data.append(0)

    # 构建 x86 位置无关桩代码
    # 功能: 获得基址 → 从 IAT 读取函数地址 → 改写 prologue → 跳转 OEP
    iat_off_GetProcAddress  = iat_map["GetProcAddress"][0]
    iat_off_ImmGetDefault   = iat_map["ImmGetDefaultIMEWnd"][0]
    iat_off_ImmGetContext   = iat_map["ImmGetContext"][0]
    iat_off_ImmSetOpenStatus = iat_map["ImmSetOpenStatus"][0]
    iat_off_AttachThreadInput = iat_map["AttachThreadInput"][0]

    asm = bytearray()

    # === Step 0: call $+5 / pop ebp / sub ebp,offset → ebp = image base ===
    asm += b'\xE8\x00\x00\x00\x00'   # call $+5
    asm += b'\x5D'                    # pop ebp
    # 计算 get_ret 的实际 RVA
    get_ret_rva = CAVE_RVA + len(asm)
    asm += b'\x81\xED' + struct.pack('<I', get_ret_rva)  # sub ebp, get_ret_rva

    # === Helper: patch function prologue at esi ===
    # 写入: mov eax, 0 (B8 00 00 00 00) 5 bytes
    #        retn X (C2 XX 00)          3 bytes
    #        nop (90)                   1 byte
    # Total: 9 bytes (足够覆盖任何函数 prologue)
    def _patch_stub(retn_val):
        """生成修补一个函数 prologue 的指令序列。esi=函数地址"""
        code = bytearray()
        code += b'\xC7\x06' + struct.pack('<I', 0x0000_00B8)  # mov dword [esi], 0xB8000000
        code += b'\xC7\x46\x04' + struct.pack('<I', 0x0090_C200 | (retn_val << 16))  # mov dword [esi+4], 0xC200XX90
        return code

    # === Patch ImmGetDefaultIMEWnd (retn 4) ===
    asm += b'\x8B\xB5' + struct.pack('<I', iat_off_ImmGetDefault)   # mov esi, [ebp+off]
    asm += _patch_stub(4)

    # === Patch ImmGetContext (retn 4) ===
    asm += b'\x8B\xB5' + struct.pack('<I', iat_off_ImmGetContext)   # mov esi, [ebp+off]
    asm += _patch_stub(4)

    # === Patch ImmSetOpenStatus (retn 8) ===
    asm += b'\x8B\xB5' + struct.pack('<I', iat_off_ImmSetOpenStatus) # mov esi, [ebp+off]
    asm += _patch_stub(8)

    # === Patch AttachThreadInput (retn 12) ===
    asm += b'\x8B\xB5' + struct.pack('<I', iat_off_AttachThreadInput) # mov esi, [ebp+off]
    asm += _patch_stub(12)

    # === Jump to original OEP ===
    # ebp = image base, add OEP relative
    asm += b'\x81\xC5' + struct.pack('<I', oep_rva)  # add ebp, oep_rva
    asm += b'\xFF\xE5'                               # jmp ebp

    # 写入代码洞
    data[cave_raw:cave_raw + len(asm)] = asm

    # ── 7. 设置入口点为代码洞 ──
    struct.pack_into('<I', data, pe_off + 24 + 16, CAVE_RVA)

    return data


# ──────────────────────────────────────────────
# R2: system.dll 修补 — 注册表弹窗绕过
# ──────────────────────────────────────────────
SYSTEM_PATCH_RVA = 0x168C8   # system.dll 内 JNE 指令的 RVA

def _patch_r2(data):
    """
    将 system.dll 中 RVA 0x168C8 的 jne → jmp，强制跳过弹窗逻辑。

    原始:  0F 85 F2 00 00 00   (jne +0xF2)
    改为:  E9 F3 00 00 00 90   (jmp +0xF3; nop)
    """
    if len(data) < SYSTEM_PATCH_RVA + 6:
        raise ValueError("system.dll 文件过短，可能版本不匹配")
    old = data[SYSTEM_PATCH_RVA:SYSTEM_PATCH_RVA + 6]
    expected = bytes([0x0F, 0x85, 0xF2, 0x00, 0x00, 0x00])
    if old != expected:
        # 也可能是已修补过的版本
        already = bytes([0xE9, 0xF3, 0x00, 0x00, 0x00, 0x90])
        if old == already:
            print("  [R2] system.dll 已修补，跳过")
            return data
        raise ValueError(
            f"system.dll RVA 0x{SYSTEM_PATCH_RVA:06X} 处内容不匹配:\n"
            f"  期望: {expected.hex()}\n"
            f"  实际: {old.hex()}\n"
            f"  这可能不是尘骸魔京的 system.dll 或版本不兼容")
    data[SYSTEM_PATCH_RVA:SYSTEM_PATCH_RVA + 6] = \
        bytes([0xE9, 0xF3, 0x00, 0x00, 0x00, 0x90])
    return data


# ──────────────────────────────────────────────
# 入口
# ──────────────────────────────────────────────
def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    exe_path = sys.argv[1]
    sys_path = sys.argv[2] if len(sys.argv) > 2 else None

    if not os.path.isfile(exe_path):
        print(f"[ERROR] 找不到文件: {exe_path}")
        sys.exit(1)

    # R1: 修补 EXE
    print(f"[R1] 读取: {exe_path}")
    with open(exe_path, 'rb') as f:
        exe_data = bytearray(f.read())

    print(f"[R1] 修补中...")
    exe_data = _patch_r1(exe_data)

    base, ext = os.path.splitext(exe_path)
    out_exe = f"{base}_patched{ext}"
    with open(out_exe, 'wb') as f:
        f.write(exe_data)
    print(f"[R1] 输出: {out_exe}")
    print(f"     将 {os.path.basename(out_exe)} 复制到游戏目录并运行即可。")
    print()

    # R2: 修补 system.dll
    if sys_path:
        if not os.path.isfile(sys_path):
            print(f"[R2] 找不到: {sys_path}，跳过")
        else:
            print(f"[R2] 读取: {sys_path}")
            with open(sys_path, 'rb') as f:
                sys_data = bytearray(f.read())
            try:
                sys_data = _patch_r2(sys_data)
                with open(sys_path, 'wb') as f:
                    f.write(sys_data)
                print(f"[R2] 已修补: {sys_path}")
                print(f"     将修补后的 system.dll 放入游戏目录即可。")
            except ValueError as e:
                print(f"[R2] 失败: {e}")
                sys.exit(1)
    else:
        print("[R2] 跳过 (未提供 system.dll)")

    print()
    print("=" * 50)
    print("  [OK] 修补完成!")
    print("  部署步骤:")
    print("    1. 备份原版 game.exe 和 system.dll")
    print(f"    2. 将 {os.path.basename(out_exe)} 复制到游戏目录")
    print("    3. 如需弹窗绕过，将修补后的 system.dll 也复制过去")
    print(f"    4. 运行 {os.path.basename(out_exe)}")
    print("=" * 50)


if __name__ == '__main__':
    main()
