#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BBR.exe compatibility fixes v2
==============================

Final scope:
  1) remove the obsolete comap.dat + reg.exe disc-auth wrapper;
  2) fix the family of GetDIBits 32bpp -> undersized packed-DIB problems that
     appear with modern 32bpp desktops / cnc-ddraw in Config, save/load menus,
     quick-save thumbnail/background capture, and similar menu paths.

Unlike v1, this build is NOT limited to one 640x480 call.  It locates the
BBR.exe GetDIBits import, finds all static calls to that import (plus the
previously proven call ending at VA 0x00559E67), and redirects them to one
strict guard stub.

The guard only changes a request when ALL of these are true:
  - BITMAPINFOHEADER size == 40
  - width is 1..640
  - absolute height is 1..480
  - start scan == 0 and cScanLines == abs(height) (full bitmap)
  - planes == 1
  - requested bit depth == 32
  - compression == BI_RGB
  - usage == DIB_RGB_COLORS
  - lpvBits == lpbi + 40 (BBR's packed header+pixel-buffer layout)

For that exact shape it changes biBitCount to 24 and computes the correct
4-byte-aligned 24bpp biSizeImage dynamically:

    stride = (width * 3 + 3) & ~3
    size   = stride * abs(height)

Everything else is forwarded unchanged to the original GDI32!GetDIBits.

No DirectDraw/window/DPI patching is included. cnc-ddraw remains responsible
for rendering/window management.

Usage:
    python bbr_07_fix_v2.py BBR.exe
    python bbr_07_fix_v2.py BBR.exe --analyze

Output:
    BBR.fixed.exe

The source EXE is never overwritten. Unknown builds are refused; there is no
--force option.
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import struct
import sys
from dataclasses import dataclass

# ---------------------------------------------------------------------------
# Verified target identity
# ---------------------------------------------------------------------------
EXPECTED_SIZE = 1_966_080
EXPECTED_SHA256 = "e7bfefeb3479c44d4ba09054575ca8d033d925b2a4d6ed7e02d7fe98228613be"
EXPECTED_TIMESTAMP = 0x4566C343
EXPECTED_IMAGE_BASE = 0x00400000
EXPECTED_SIZE_OF_IMAGE = 0x00280000

# Verified disc-auth wrapper.
AUTH_OFFSET = 0x136E20
AUTH_OLD = bytes.fromhex("55 8B EC 83 EC 44")
AUTH_NEW = bytes.fromhex("B8 01 00 00 00 C3")

# Previously proven crashing GetDIBits call return address.
KNOWN_GETDIBITS_RETURN_VA = 0x00559E67

MARKER = b"BBR_07_FIX_AUTH_GDI_ALL_V2\x00"
SECTION_NAME = b".bbrfix\x00"
SECTION_CHARACTERISTICS = 0x60000020  # CODE | EXECUTE | READ

# Guard bounds: BBR's native render area. Smaller menu thumbnails are included.
MAX_DIB_WIDTH = 640
MAX_DIB_HEIGHT = 480


def u16(b: bytes | bytearray, o: int) -> int:
    return struct.unpack_from("<H", b, o)[0]


def u32(b: bytes | bytearray, o: int) -> int:
    return struct.unpack_from("<I", b, o)[0]


def p16(b: bytearray, o: int, v: int) -> None:
    struct.pack_into("<H", b, o, v & 0xFFFF)


def p32(b: bytearray, o: int, v: int) -> None:
    struct.pack_into("<I", b, o, v & 0xFFFFFFFF)


def align_up(v: int, a: int) -> int:
    if a <= 0:
        raise ValueError("无效的 PE 对齐值")
    return (v + a - 1) & ~(a - 1)


@dataclass
class Section:
    index: int
    hdr_off: int
    name: bytes
    virtual_size: int
    virtual_address: int
    raw_size: int
    raw_ptr: int
    characteristics: int


@dataclass
class ImportEntry:
    dll: str
    name: str
    iat_rva: int
    iat_va: int


@dataclass
class CallSite:
    va: int
    raw_off: int
    size: int
    kind: str
    original: bytes


class PE32:
    def __init__(self, data: bytearray):
        self.data = data
        if len(data) < 0x100 or data[:2] != b"MZ":
            raise ValueError("不是有效的 MZ/PE 文件")

        self.pe_off = u32(data, 0x3C)
        if (self.pe_off + 0x100 > len(data) or
                data[self.pe_off:self.pe_off + 4] != b"PE\0\0"):
            raise ValueError("PE 头无效")

        self.file_hdr = self.pe_off + 4
        self.machine = u16(data, self.file_hdr)
        self.num_sections = u16(data, self.file_hdr + 2)
        self.timestamp = u32(data, self.file_hdr + 4)
        self.opt_size = u16(data, self.file_hdr + 16)
        self.opt = self.file_hdr + 20

        if self.machine != 0x14C or u16(data, self.opt) != 0x10B:
            raise ValueError("只支持 Win32/x86 PE32")

        self.image_base = u32(data, self.opt + 28)
        self.section_alignment = u32(data, self.opt + 32)
        self.file_alignment = u32(data, self.opt + 36)
        self.size_of_image = u32(data, self.opt + 56)
        self.size_of_headers = u32(data, self.opt + 60)
        self.checksum_off = self.opt + 64
        self.section_table = self.opt + self.opt_size

        self.sections: list[Section] = []
        for i in range(self.num_sections):
            o = self.section_table + i * 40
            if o + 40 > len(data):
                raise ValueError("Section table 越界")
            self.sections.append(Section(
                index=i,
                hdr_off=o,
                name=bytes(data[o:o + 8]).split(b"\0", 1)[0],
                virtual_size=u32(data, o + 8),
                virtual_address=u32(data, o + 12),
                raw_size=u32(data, o + 16),
                raw_ptr=u32(data, o + 20),
                characteristics=u32(data, o + 36),
            ))

    def rva_to_raw(self, rva: int) -> int:
        if 0 <= rva < self.size_of_headers:
            return rva
        for s in self.sections:
            span = max(s.virtual_size, s.raw_size)
            if s.virtual_address <= rva < s.virtual_address + span:
                d = rva - s.virtual_address
                if d >= s.raw_size:
                    raise ValueError(f"RVA 0x{rva:X} 位于未落盘区域")
                return s.raw_ptr + d
        raise ValueError(f"无法映射 RVA 0x{rva:X}")

    def va_to_raw(self, va: int) -> int:
        return self.rva_to_raw(va - self.image_base)

    def raw_to_va(self, off: int) -> int:
        for s in self.sections:
            if s.raw_ptr <= off < s.raw_ptr + s.raw_size:
                return self.image_base + s.virtual_address + (off - s.raw_ptr)
        if off < self.size_of_headers:
            return self.image_base + off
        raise ValueError(f"无法映射文件偏移 0x{off:X}")

    def executable_sections(self) -> list[Section]:
        IMAGE_SCN_MEM_EXECUTE = 0x20000000
        return [s for s in self.sections if s.characteristics & IMAGE_SCN_MEM_EXECUTE and s.raw_size]

    def imports(self) -> list[ImportEntry]:
        # PE32 data-directory base is OptionalHeader + 96; import directory index=1.
        import_rva = u32(self.data, self.opt + 96 + 8)
        if not import_rva:
            return []
        desc_off = self.rva_to_raw(import_rva)
        out: list[ImportEntry] = []

        for _ in range(128):
            oft, _ts, _fc, name_rva, first_thunk = struct.unpack_from(
                "<IIIII", self.data, desc_off
            )
            if oft == 0 and name_rva == 0 and first_thunk == 0:
                break

            name_off = self.rva_to_raw(name_rva)
            end = self.data.find(b"\0", name_off)
            if end < 0:
                raise ValueError("损坏的 import DLL 名称")
            dll = bytes(self.data[name_off:end]).decode("ascii", "replace")

            thunk_rva = oft or first_thunk
            idx = 0
            while True:
                toff = self.rva_to_raw(thunk_rva + idx * 4)
                val = u32(self.data, toff)
                if val == 0:
                    break
                if not (val & 0x80000000):
                    noff = self.rva_to_raw(val)
                    nend = self.data.find(b"\0", noff + 2)
                    if nend < 0:
                        raise ValueError("损坏的 import 函数名")
                    fn = bytes(self.data[noff + 2:nend]).decode("ascii", "replace")
                    out.append(ImportEntry(
                        dll=dll,
                        name=fn,
                        iat_rva=first_thunk + idx * 4,
                        iat_va=self.image_base + first_thunk + idx * 4,
                    ))
                idx += 1
            desc_off += 20
        return out

    def add_executable_section(self, payload: bytes) -> tuple[int, int]:
        """Append a tiny executable/read-only section without moving old bytes."""
        new_hdr = self.section_table + self.num_sections * 40
        first_raw = min(
            (s.raw_ptr for s in self.sections if s.raw_ptr),
            default=self.size_of_headers,
        )
        if new_hdr + 40 > first_raw:
            raise ValueError("PE 头没有空间增加新的 section header")

        last_va_end = max(
            s.virtual_address + max(s.virtual_size, s.raw_size)
            for s in self.sections
        )
        new_rva = align_up(last_va_end, self.section_alignment)
        new_raw = align_up(len(self.data), self.file_alignment)
        raw_size = align_up(len(payload), self.file_alignment)
        virt_size = len(payload)

        if len(self.data) < new_raw:
            self.data.extend(b"\0" * (new_raw - len(self.data)))
        self.data.extend(payload)
        if raw_size > len(payload):
            self.data.extend(b"\0" * (raw_size - len(payload)))

        hdr = bytearray(40)
        hdr[:8] = SECTION_NAME[:8].ljust(8, b"\0")
        struct.pack_into("<I", hdr, 8, virt_size)
        struct.pack_into("<I", hdr, 12, new_rva)
        struct.pack_into("<I", hdr, 16, raw_size)
        struct.pack_into("<I", hdr, 20, new_raw)
        struct.pack_into("<I", hdr, 36, SECTION_CHARACTERISTICS)
        self.data[new_hdr:new_hdr + 40] = hdr

        p16(self.data, self.file_hdr + 2, self.num_sections + 1)
        p32(
            self.data,
            self.opt + 56,
            align_up(new_rva + virt_size, self.section_alignment),
        )
        p32(self.data, self.checksum_off, 0)
        return self.image_base + new_rva, new_raw


class CodeBuilder:
    def __init__(self):
        self.buf = bytearray()
        self.labels: dict[str, int] = {}
        self.fixups: list[tuple[int, str]] = []

    def emit(self, *xs: int) -> None:
        self.buf.extend(xs)

    def raw(self, data: bytes) -> None:
        self.buf.extend(data)

    def label(self, name: str) -> None:
        if name in self.labels:
            raise ValueError(f"重复标签：{name}")
        self.labels[name] = len(self.buf)

    def jcc32(self, opcode2: int, label: str) -> None:
        self.emit(0x0F, opcode2)
        p = len(self.buf)
        self.raw(b"\0\0\0\0")
        self.fixups.append((p, label))

    def finish(self) -> bytearray:
        for p, label in self.fixups:
            if label not in self.labels:
                raise ValueError(f"缺失标签：{label}")
            rel = self.labels[label] - (p + 4)
            struct.pack_into("<i", self.buf, p, rel)
        return self.buf


def normalized_original_for_identity(raw: bytes) -> tuple[bytes, str]:
    if len(raw) < AUTH_OFFSET + 6:
        return raw, "unknown"
    cur = raw[AUTH_OFFSET:AUTH_OFFSET + 6]
    if cur == AUTH_OLD:
        return raw, "original-auth"
    if cur == AUTH_NEW:
        n = bytearray(raw)
        n[AUTH_OFFSET:AUTH_OFFSET + 6] = AUTH_OLD
        return bytes(n), "auth-already-removed"
    return raw, "unknown-auth-bytes"


def validate_target(raw: bytes, pe: PE32) -> str:
    normalized, auth_state = normalized_original_for_identity(raw)
    sha = hashlib.sha256(normalized).hexdigest()

    print(f"[文件] size={len(raw)}")
    print(f"[SHA256] {sha}")
    print(f"[认证] {auth_state}")
    print(
        f"[PE] TimeDateStamp={pe.timestamp:08X} "
        f"ImageBase={pe.image_base:08X} SizeOfImage={pe.size_of_image:08X}"
    )

    problems: list[str] = []
    if len(raw) != EXPECTED_SIZE:
        problems.append(f"文件大小 {len(raw)} != {EXPECTED_SIZE}")
    if sha.lower() != EXPECTED_SHA256:
        problems.append("规范化 SHA-256 与已验证 BBR.exe 不匹配")
    if pe.timestamp != EXPECTED_TIMESTAMP:
        problems.append(
            f"TimeDateStamp {pe.timestamp:08X} != {EXPECTED_TIMESTAMP:08X}"
        )
    if pe.image_base != EXPECTED_IMAGE_BASE:
        problems.append(
            f"ImageBase {pe.image_base:08X} != {EXPECTED_IMAGE_BASE:08X}"
        )
    if pe.size_of_image != EXPECTED_SIZE_OF_IMAGE:
        problems.append(
            f"SizeOfImage {pe.size_of_image:08X} != {EXPECTED_SIZE_OF_IMAGE:08X}"
        )
    if auth_state == "unknown-auth-bytes":
        problems.append("认证函数位置的原字节既不是原版也不是已移除状态")

    if problems:
        print("[拒绝] 目标不是已验证的 BBR.exe：")
        for p in problems:
            print("       - " + p)
        raise ValueError("严格版本校验失败；本工具不提供 --force")
    return auth_state


def patch_auth(data: bytearray) -> str:
    cur = bytes(data[AUTH_OFFSET:AUTH_OFFSET + 6])
    if cur == AUTH_NEW:
        return "已是 mov eax,1 ; ret，跳过"
    if cur != AUTH_OLD:
        raise ValueError("认证函数原字节不匹配：" + cur.hex(" ").upper())
    data[AUTH_OFFSET:AUTH_OFFSET + 6] = AUTH_NEW
    return (
        f"0x{AUTH_OFFSET:X}: {AUTH_OLD.hex(' ').upper()} -> "
        f"{AUTH_NEW.hex(' ').upper()}"
    )


def find_getdibits_import(pe: PE32) -> ImportEntry:
    matches = [
        x for x in pe.imports()
        if x.name == "GetDIBits" and x.dll.lower().endswith("gdi32.dll")
    ]
    if len(matches) != 1:
        raise ValueError(
            f"GetDIBits 导入数量异常：{len(matches)}（预期 1）"
        )
    return matches[0]


def identify_known_call(pe: PE32) -> CallSite | None:
    ret = KNOWN_GETDIBITS_RETURN_VA
    # E8 rel32
    try:
        va = ret - 5
        off = pe.va_to_raw(va)
        if pe.data[off] == 0xE8:
            return CallSite(va, off, 5, "known-E8", bytes(pe.data[off:off + 5]))
    except Exception:
        pass
    # FF 15 imm32
    try:
        va = ret - 6
        off = pe.va_to_raw(va)
        if pe.data[off:off + 2] == b"\xFF\x15":
            return CallSite(va, off, 6, "known-FF15", bytes(pe.data[off:off + 6]))
    except Exception:
        pass
    return None


def find_getdibits_calls(pe: PE32, imp: ImportEntry) -> list[CallSite]:
    """Find static calls through GetDIBits IAT, plus E8 calls to local IAT thunks."""
    sites: dict[int, CallSite] = {}
    iat_le = struct.pack("<I", imp.iat_va)

    # First find local jmp dword ptr [GetDIBits_IAT] thunks.
    thunk_vas: set[int] = set()
    thunk_pat = b"\xFF\x25" + iat_le
    for s in pe.executable_sections():
        region = bytes(pe.data[s.raw_ptr:s.raw_ptr + s.raw_size])
        pos = 0
        while True:
            i = region.find(thunk_pat, pos)
            if i < 0:
                break
            thunk_vas.add(pe.image_base + s.virtual_address + i)
            pos = i + 1

    # Direct call dword ptr [IAT].
    call_pat = b"\xFF\x15" + iat_le
    for s in pe.executable_sections():
        region = bytes(pe.data[s.raw_ptr:s.raw_ptr + s.raw_size])
        pos = 0
        while True:
            i = region.find(call_pat, pos)
            if i < 0:
                break
            off = s.raw_ptr + i
            va = pe.image_base + s.virtual_address + i
            sites[va] = CallSite(va, off, 6, "FF15-IAT", bytes(pe.data[off:off + 6]))
            pos = i + 1

    # E8 rel32 calls that resolve exactly to an IAT thunk.
    if thunk_vas:
        for s in pe.executable_sections():
            start = s.raw_ptr
            end = s.raw_ptr + s.raw_size - 5
            off = start
            while off <= end:
                if pe.data[off] == 0xE8:
                    rel = struct.unpack_from("<i", pe.data, off + 1)[0]
                    va = pe.image_base + s.virtual_address + (off - s.raw_ptr)
                    target = va + 5 + rel
                    if target in thunk_vas:
                        sites[va] = CallSite(va, off, 5, "E8-IAT-thunk", bytes(pe.data[off:off + 5]))
                off += 1

    # Preserve the already-proven Config site even if the compiler reached the
    # import through another local thunk shape not caught above.
    known = identify_known_call(pe)
    if known is not None:
        sites.setdefault(known.va, known)

    return [sites[k] for k in sorted(sites)]


def build_getdibits_guard(stub_va: int, getdibits_iat_va: int) -> bytes:
    """Build generic BBR packed-DIB 32->24 compatibility guard."""
    c = CodeBuilder()

    # GetDIBits(hdc,hbmp,uStartScan,cScanLines,lpvBits,lpbi,uUsage)
    # lpbi = [esp+18h]
    c.raw(b"\x8B\x44\x24\x18")                  # mov eax,[esp+18h]
    c.raw(b"\x85\xC0")                            # test eax,eax
    c.jcc32(0x84, "orig")

    c.raw(b"\x83\x38\x28")                       # biSize == 40
    c.jcc32(0x85, "orig")

    # width 1..640
    c.raw(b"\x8B\x48\x04")                       # mov ecx,[eax+4]
    c.raw(b"\x85\xC9")                            # test ecx,ecx
    c.jcc32(0x8E, "orig")                          # jle
    c.raw(b"\x81\xF9" + struct.pack("<I", MAX_DIB_WIDTH))
    c.jcc32(0x87, "orig")                          # ja

    # abs(height) 1..480 in edx
    c.raw(b"\x8B\x50\x08")                       # mov edx,[eax+8]
    c.raw(b"\x85\xD2")
    c.jcc32(0x84, "orig")                          # zero height
    c.jcc32(0x89, "height_pos")                    # jns
    c.raw(b"\xF7\xDA")                            # neg edx
    c.raw(b"\x85\xD2")
    c.jcc32(0x8E, "orig")                          # INT_MIN / invalid
    c.label("height_pos")
    c.raw(b"\x81\xFA" + struct.pack("<I", MAX_DIB_HEIGHT))
    c.jcc32(0x87, "orig")

    # full-image extraction only: start=0, cScanLines=abs(height)
    c.raw(b"\x83\x7C\x24\x0C\x00")             # cmp [esp+0Ch],0
    c.jcc32(0x85, "orig")
    c.raw(b"\x39\x54\x24\x10")                  # cmp [esp+10h],edx
    c.jcc32(0x85, "orig")

    c.raw(b"\x66\x83\x78\x0C\x01")             # planes == 1
    c.jcc32(0x85, "orig")
    c.raw(b"\x66\x83\x78\x0E\x20")             # bitcount == 32
    c.jcc32(0x85, "orig")
    c.raw(b"\x83\x78\x10\x00")                  # BI_RGB
    c.jcc32(0x85, "orig")
    c.raw(b"\x83\x7C\x24\x1C\x00")             # DIB_RGB_COLORS
    c.jcc32(0x85, "orig")

    # Packed BBR allocation: [BITMAPINFOHEADER(40)][pixel buffer]
    c.raw(b"\x8B\x4C\x24\x14")                  # ecx=lpvBits
    c.raw(b"\x85\xC9")
    c.jcc32(0x84, "orig")
    c.raw(b"\x8D\x50\x28")                       # edx=lpbi+40
    c.raw(b"\x3B\xCA")                            # cmp ecx,edx
    c.jcc32(0x85, "orig")

    # Dynamic 24bpp size: stride=((width*3)+3)&~3; size=stride*abs(height)
    c.raw(b"\x8B\x48\x04")                       # ecx=width
    c.raw(b"\x8D\x0C\x49")                       # ecx=width*3
    c.raw(b"\x83\xC1\x03")                       # +3
    c.raw(b"\x83\xE1\xFC")                       # &~3
    c.raw(b"\x8B\x50\x08")                       # edx=height
    c.raw(b"\x85\xD2")
    c.jcc32(0x89, "size_hpos")
    c.raw(b"\xF7\xDA")                            # neg edx
    c.label("size_hpos")
    c.raw(b"\x0F\xAF\xCA")                       # imul ecx,edx

    c.raw(b"\x66\xC7\x40\x0E\x18\x00")        # biBitCount=24
    c.raw(b"\x89\x48\x14")                       # biSizeImage=ecx

    c.label("orig")
    body = c.finish()
    # Tail-jump through original IAT: leaves caller's return address/args intact.
    body.extend(b"\xFF\x25" + struct.pack("<I", getdibits_iat_va))
    body.extend(MARKER)
    return bytes(body)


def patch_call_to_stub(pe: PE32, site: CallSite, stub_va: int) -> bytes:
    rel = stub_va - (site.va + 5)
    patch = b"\xE8" + struct.pack("<i", rel)
    if site.size == 6:
        patch += b"\x90"
    if len(patch) != site.size:
        raise ValueError("内部错误：调用点补丁长度不匹配")
    pe.data[site.raw_off:site.raw_off + site.size] = patch
    return patch


def patch_file(src: pathlib.Path, dst: pathlib.Path, analyze_only: bool) -> None:
    raw = src.read_bytes()
    if MARKER in raw or SECTION_NAME.rstrip(b"\0") in raw[:0x2000]:
        raise ValueError("该 EXE 已包含 BBR GDI 修复，拒绝重复注入；请从原版重新生成")

    pe = PE32(bytearray(raw))
    validate_target(raw, pe)

    imp = find_getdibits_import(pe)
    print(f"[导入] {imp.dll}!{imp.name} IAT=0x{imp.iat_va:08X}")

    sites = find_getdibits_calls(pe, imp)
    if not sites:
        raise ValueError("未找到任何可安全重定向的 GetDIBits 调用点")

    print(f"[扫描] GetDIBits 静态调用点：{len(sites)} 个")
    for i, s in enumerate(sites, 1):
        mark = "  <已知 Config 点>" if s.va + s.size == KNOWN_GETDIBITS_RETURN_VA else ""
        print(
            f"       #{i:02d} VA=0x{s.va:08X} kind={s.kind:<13} "
            f"bytes={s.original.hex(' ').upper()}{mark}"
        )

    known_ok = any(s.va + s.size == KNOWN_GETDIBITS_RETURN_VA for s in sites)
    if not known_ok:
        raise ValueError("已验证的 Config GetDIBits 调用点未出现在扫描结果中，拒绝修改")

    auth_cur = bytes(pe.data[AUTH_OFFSET:AUTH_OFFSET + 6])
    print(f"[定位] 光盘认证 offset=0x{AUTH_OFFSET:X} bytes={auth_cur.hex(' ').upper()}")
    print(
        "[策略] 对所有上述 GetDIBits 调用应用同一个严格 guard；"
        "尺寸动态计算，不再只限 640x480。"
    )

    if analyze_only:
        print("[分析] 校验与扫描完成；未修改任何文件。")
        return

    dummy = build_getdibits_guard(0x10000000, imp.iat_va)
    stub_va, stub_raw = pe.add_executable_section(dummy)
    stub = build_getdibits_guard(stub_va, imp.iat_va)
    if len(stub) != len(dummy):
        raise ValueError("内部错误：stub 长度在重定位后改变")
    pe.data[stub_raw:stub_raw + len(stub)] = stub

    patched = []
    for s in sites:
        newb = patch_call_to_stub(pe, s, stub_va)
        patched.append((s, newb))

    auth_msg = patch_auth(pe.data)
    dst.write_bytes(pe.data)

    print(f"[补丁] 光盘认证：{auth_msg}")
    print(f"[补丁] GDI guard VA=0x{stub_va:08X} raw=0x{stub_raw:X} size={len(stub)}")
    print(f"[补丁] 已重定向 GetDIBits 调用点：{len(patched)} 个")
    for s, nb in patched:
        print(f"       0x{s.va:08X}: {s.original.hex(' ').upper()} -> {nb.hex(' ').upper()}")
    print(
        "[Guard] 仅处理：完整 DIB、1..640 x 1..480、32bpp BI_RGB、"
        "DIB_RGB_COLORS、lpvBits=lpbi+40；动态改为 24bpp。"
    )
    print("[保留] 不修改 DirectDraw/窗口/DPI；继续由 cnc-ddraw 管理显示。")
    print(f"[完成] {dst}")
    print("[测试] 请重点测试 Config、游戏内菜单、存档、读档、快速存档，以及连续多次保存/读取。")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="BBR.exe v2：移除旧式认证 + 全菜单 GetDIBits 兼容修复"
    )
    ap.add_argument(
        "exe", nargs="?", default="BBR.exe",
        help="原始 BBR.exe 或仅已移除光盘认证的 BBR.exe（默认 BBR.exe）",
    )
    ap.add_argument("-o", "--output", help="输出路径（默认 BBR.fixed.exe）")
    ap.add_argument(
        "--analyze", action="store_true",
        help="只做严格版本校验、GetDIBits 导入与调用点扫描，不写文件",
    )
    ns = ap.parse_args()

    src = pathlib.Path(ns.exe)
    if not src.is_file():
        print(f"[错误] 找不到文件：{src}", file=sys.stderr)
        return 2

    dst = pathlib.Path(ns.output) if ns.output else src.with_name(src.stem + ".fixed" + src.suffix)
    if not ns.analyze and dst.resolve() == src.resolve():
        print("[错误] 输出路径不能覆盖输入 EXE。", file=sys.stderr)
        return 2

    try:
        patch_file(src, dst, ns.analyze)
        return 0
    except Exception as e:
        print(f"[错误] {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
