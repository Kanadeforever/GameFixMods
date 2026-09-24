#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BBR.exe 2007-era compatibility fixes (merged)
===============================================

Target
------
BALDR BULLET REVELLION / BBR.exe (verified 2006 build).

This single patcher merges ONLY the two fixes that have been kept:

1) Remove the obsolete comap.dat + reg.exe disc-authentication wrapper.
   The verified BBR authentication function at file offset 0x136E20 is changed
   from its original prologue to:

       B8 01 00 00 00 C3      mov eax,1 ; ret

   so the game treats the disc check as successful without invoking the old
   reg.exe path.

2) Fix the cnc-ddraw Config-screen GetDIBits buffer overrun.
   Only the GetDIBits call that returns to VA 0x00559E67 is redirected to a
   small guard stub.  For the exact dangerous 640x480/32-bpp DIB case, the
   request is changed to 24-bpp and biSizeImage=0xE1000 before forwarding to
   the original GetDIBits target.

Out of scope
------------
- No built-in window mode.
- No DirectDraw replacement.
- No SetDisplayMode/CooperativeLevel patching.
- No DPI changes.
- cnc-ddraw remains responsible for modern rendering/window management.

Input
-----
The script accepts either:
- the verified original BBR.exe; or
- the same BBR.exe with ONLY the disc-auth patch already applied.

It deliberately refuses unknown builds.  There is no --force option.

Usage
-----
    python bbr_07_fix.py BBR.exe

Output
------
    BBR.fixed.exe

The input EXE is never overwritten.
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import struct
import sys
from dataclasses import dataclass

# ---------------------------------------------------------------------------
# Verified BBR identity
# ---------------------------------------------------------------------------
EXPECTED_SIZE = 1_966_080
EXPECTED_SHA256 = "e7bfefeb3479c44d4ba09054575ca8d033d925b2a4d6ed7e02d7fe98228613be"
EXPECTED_TIMESTAMP = 0x4566C343
EXPECTED_IMAGE_BASE = 0x00400000
EXPECTED_SIZE_OF_IMAGE = 0x00280000

# Disc-auth patch verified for BBR.exe.
AUTH_OFFSET = 0x136E20
AUTH_OLD = bytes.fromhex("55 8B EC 83 EC 44")
AUTH_NEW = bytes.fromhex("B8 01 00 00 00 C3")

# Config crash evidence from cnc-ddraw dump.
GETDIBITS_RETURN_VA = 0x00559E67
MARKER = b"BBR_07_FIX_AUTH_CONFIG_V1\x00"
SECTION_NAME = b".bbrfix\x00"

# IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ
SECTION_CHARACTERISTICS = 0x60000020


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

    def va_to_raw(self, va: int) -> int:
        rva = va - self.image_base
        if 0 <= rva < self.size_of_headers:
            return rva
        for s in self.sections:
            span = max(s.virtual_size, s.raw_size)
            if s.virtual_address <= rva < s.virtual_address + span:
                delta = rva - s.virtual_address
                if delta >= s.raw_size:
                    raise ValueError(f"VA 0x{va:08X} 位于未落盘区域")
                return s.raw_ptr + delta
        raise ValueError(f"无法把 VA 0x{va:08X} 映射到文件偏移")

    def add_executable_section(self, payload: bytes) -> tuple[int, int]:
        """Append one executable/read-only section without moving old file bytes."""
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
        # Do not retain a stale PE checksum.
        p32(self.data, self.checksum_off, 0)

        return self.image_base + new_rva, new_raw


@dataclass
class OriginalCall:
    call_va: int
    call_size: int
    kind: str       # "direct" or "iat"
    target: int     # direct target VA or IAT slot VA
    raw_bytes: bytes


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
        pos = len(self.buf)
        self.raw(b"\0\0\0\0")
        self.fixups.append((pos, label))

    def finish(self) -> bytearray:
        for pos, label in self.fixups:
            if label not in self.labels:
                raise ValueError(f"缺失标签：{label}")
            rel = self.labels[label] - (pos + 4)
            struct.pack_into("<i", self.buf, pos, rel)
        return self.buf


def identify_getdibits_call(pe: PE32) -> OriginalCall:
    """Identify the API call whose return address is 0x00559E67."""
    ret = GETDIBITS_RETURN_VA

    # E8 rel32
    va5 = ret - 5
    try:
        o5 = pe.va_to_raw(va5)
        if pe.data[o5] == 0xE8:
            rel = struct.unpack_from("<i", pe.data, o5 + 1)[0]
            return OriginalCall(
                call_va=va5,
                call_size=5,
                kind="direct",
                target=ret + rel,
                raw_bytes=bytes(pe.data[o5:o5 + 5]),
            )
    except Exception:
        pass

    # FF 15 imm32  => call dword ptr [IAT]
    va6 = ret - 6
    try:
        o6 = pe.va_to_raw(va6)
        if pe.data[o6:o6 + 2] == b"\xFF\x15":
            return OriginalCall(
                call_va=va6,
                call_size=6,
                kind="iat",
                target=u32(pe.data, o6 + 2),
                raw_bytes=bytes(pe.data[o6:o6 + 6]),
            )
    except Exception:
        pass

    try:
        o = pe.va_to_raw(ret - 12)
        ctx = bytes(pe.data[o:o + 24]).hex(" ").upper()
    except Exception:
        ctx = "<不可用>"
    raise ValueError(
        "无法识别返回到 0x00559E67 的 GetDIBits 调用；"
        f"附近机器码：{ctx}"
    )


def build_getdibits_guard(stub_va: int, original: OriginalCall) -> bytes:
    """Build exact 640x480 32bpp -> 24bpp guard, then tail-call original API."""
    c = CodeBuilder()

    # GetDIBits(hdc,hbmp,start,cLines,lpvBits,lpbi,usage)
    # At function entry, lpbi is [esp+18h].
    c.raw(b"\x8B\x44\x24\x18")                    # mov eax,[esp+18h]
    c.raw(b"\x85\xC0")                              # test eax,eax
    c.jcc32(0x84, "orig")

    c.raw(b"\x83\x38\x28")                         # biSize == 40
    c.jcc32(0x85, "orig")

    c.raw(b"\x81\x78\x04\x80\x02\x00\x00")  # width == 640
    c.jcc32(0x85, "orig")

    c.raw(b"\x8B\x48\x08")                         # ecx = height
    c.raw(b"\x81\xF9\xE0\x01\x00\x00")       # +480
    c.jcc32(0x84, "height_ok")
    c.raw(b"\x81\xF9\x20\xFE\xFF\xFF")       # -480
    c.jcc32(0x85, "orig")

    c.label("height_ok")
    c.raw(b"\x66\x83\x78\x0C\x01")              # planes == 1
    c.jcc32(0x85, "orig")
    c.raw(b"\x66\x83\x78\x0E\x20")              # bitcount == 32
    c.jcc32(0x85, "orig")
    c.raw(b"\x83\x78\x10\x00")                   # compression == BI_RGB
    c.jcc32(0x85, "orig")

    c.raw(b"\x81\x7C\x24\x10\xE0\x01\x00\x00")  # cLines == 480
    c.jcc32(0x85, "orig")

    c.raw(b"\x8B\x54\x24\x14")                    # edx = lpvBits
    c.raw(b"\x8D\x48\x28")                         # ecx = lpbi + 40
    c.raw(b"\x3B\xD1")                              # lpvBits == lpbi+40
    c.jcc32(0x85, "orig")

    c.raw(b"\x66\xC7\x40\x0E\x18\x00")         # biBitCount = 24
    c.raw(b"\xC7\x40\x14\x00\x10\x0E\x00")  # biSizeImage = 0xE1000

    c.label("orig")
    body = c.finish()
    tail_off = len(body)

    if original.kind == "direct":
        # jmp original function
        rel = original.target - (stub_va + tail_off + 5)
        body.extend(b"\xE9" + struct.pack("<i", rel))
    elif original.kind == "iat":
        # jmp dword ptr [IAT]
        body.extend(b"\xFF\x25" + struct.pack("<I", original.target))
    else:
        raise ValueError("未知的原始 GetDIBits 调用类型")

    body.extend(MARKER)
    return bytes(body)


def normalized_original_for_identity(raw: bytes) -> tuple[bytes, str]:
    """Normalize an auth-only-patched BBR back to original bytes for SHA check."""
    if len(raw) < AUTH_OFFSET + len(AUTH_NEW):
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
        raise ValueError(
            "认证函数原字节不匹配：" + cur.hex(" ").upper()
        )
    data[AUTH_OFFSET:AUTH_OFFSET + 6] = AUTH_NEW
    return (
        f"0x{AUTH_OFFSET:X}: {AUTH_OLD.hex(' ').upper()} -> "
        f"{AUTH_NEW.hex(' ').upper()}"
    )


def patch_file(src: pathlib.Path, dst: pathlib.Path, analyze_only: bool) -> None:
    raw = src.read_bytes()

    if MARKER in raw or SECTION_NAME.rstrip(b"\0") in raw[:0x2000]:
        raise ValueError("该 EXE 已包含 Config 越界修复，拒绝重复注入")

    pe = PE32(bytearray(raw))
    validate_target(raw, pe)

    call = identify_getdibits_call(pe)
    print(
        f"[定位] GetDIBits call VA=0x{call.call_va:08X} "
        f"bytes={call.raw_bytes.hex(' ').upper()} kind={call.kind}"
    )
    if call.kind == "direct":
        print(f"[定位] 原目标 direct VA=0x{call.target:08X}")
    else:
        print(f"[定位] 原目标 IAT slot VA=0x{call.target:08X}")

    auth_cur = bytes(pe.data[AUTH_OFFSET:AUTH_OFFSET + 6])
    print(f"[定位] 光盘认证 file offset=0x{AUTH_OFFSET:X} bytes={auth_cur.hex(' ').upper()}")

    if analyze_only:
        print("[分析] 校验与定位均通过；未修改任何文件。")
        return

    # Build a dummy payload to reserve a section, then rebuild using its real VA.
    dummy = build_getdibits_guard(0x10000000, call)
    stub_va, stub_raw = pe.add_executable_section(dummy)
    stub = build_getdibits_guard(stub_va, call)
    if len(stub) != len(dummy):
        raise ValueError("内部错误：stub 长度在重定位后发生变化")
    pe.data[stub_raw:stub_raw + len(stub)] = stub

    # Redirect only the dangerous GetDIBits call.
    call_raw = pe.va_to_raw(call.call_va)
    rel = stub_va - (call.call_va + 5)
    call_patch = b"\xE8" + struct.pack("<i", rel)
    if call.call_size == 6:
        call_patch += b"\x90"
    if len(call_patch) != call.call_size:
        raise ValueError("内部错误：call 补丁长度不匹配")
    pe.data[call_raw:call_raw + call.call_size] = call_patch

    auth_msg = patch_auth(pe.data)

    dst.write_bytes(pe.data)

    print(f"[补丁] 光盘认证：{auth_msg}")
    print(
        f"[补丁] Config guard stub VA=0x{stub_va:08X} "
        f"raw=0x{stub_raw:X} size={len(stub)}"
    )
    print(f"[补丁] GetDIBits call -> {call_patch.hex(' ').upper()}")
    print("[保留] 不修改 DirectDraw/窗口模式；继续由 cnc-ddraw 管理显示和窗口化。")
    print(f"[完成] {dst}")
    print("[说明] 原始 EXE 未覆盖。需要时将输出文件改名为 BBR.exe 使用。")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="BBR.exe 合并修复：移除旧式光盘认证 + cnc-ddraw Config 越界修复"
    )
    ap.add_argument(
        "exe", nargs="?", default="BBR.exe",
        help="原始 BBR.exe 或仅已移除光盘认证的 BBR.exe（默认 BBR.exe）",
    )
    ap.add_argument(
        "-o", "--output",
        help="输出路径（默认 BBR.fixed.exe）",
    )
    ap.add_argument(
        "--analyze", action="store_true",
        help="只做严格校验和定位，不生成补丁文件",
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
