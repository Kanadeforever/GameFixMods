#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BALDR SKY SteamFix 特征码离线扫描器
===================================

用途：
    在“不运行游戏、不修改 EXE”的情况下，检查某个 BaldrSky.exe 是否符合
    BaldrSkyWin11Fix v0.1-test8 所使用的 SteamFix 语义签名。

设计原则：
    1. 只读取 EXE，绝不写入 EXE。
    2. 不依赖 pefile、capstone 等第三方模块，只用 Python 标准库。
    3. 不依赖中文版固定地址。
    4. 中文版和英文版允许 .patch 区布局不同。
    5. 每一个关键结构都必须“唯一命中 1 次”才判定 PASS。

这个文件中的注释故意写得非常细，因为项目要求任何源码都必须能让
只学过很少编程基础的人顺着注释理解“每一步到底在做什么”。
"""

from pathlib import Path
import struct
import sys


# ---------------------------------------------------------------------------
# 最基础的二进制读取函数
# ---------------------------------------------------------------------------
# PE 文件里的数字使用“小端序（little endian）”。
# 举例：字节 78 56 34 12 代表的 32 位整数是 0x12345678。
# struct.unpack_from 可以按指定格式从一串 bytes 中直接读出数字。

def read_u16(data: bytes, offset: int) -> int:
    """从 data[offset] 开始读取一个无符号 16 位小端整数。"""
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes | bytearray, offset: int) -> int:
    """从指定位置读取一个无符号 32 位小端整数。"""
    return struct.unpack_from("<I", data, offset)[0]


def read_s32(data: bytes | bytearray, offset: int) -> int:
    """从指定位置读取一个有符号 32 位小端整数，主要用于 x86 rel32。"""
    return struct.unpack_from("<i", data, offset)[0]


# ---------------------------------------------------------------------------
# 把磁盘上的 PE 文件“映射”为接近 Windows 内存中的布局
# ---------------------------------------------------------------------------
# 磁盘 PE 的节数据不一定放在它最终的 RVA（Relative Virtual Address）位置。
# 例如 .text 可能在磁盘偏移 0x400，但加载到内存后是 RVA 0x1000。
#
# SteamFix 扫描器运行时扫描的是“内存映像”，所以离线扫描工具也必须把磁盘文件
# 重新排成同样的 RVA 布局。这样离线算出的地址才能和运行时逻辑一致。

def map_pe_image(exe_path: Path):
    raw = exe_path.read_bytes()

    # DOS 头最前面必须是 MZ。
    if raw[:2] != b"MZ":
        raise ValueError("不是有效 PE：缺少 MZ 头")

    # DOS 头 0x3C 处保存 PE Header 的磁盘偏移。
    pe_offset = read_u32(raw, 0x3C)
    if raw[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ValueError("不是有效 PE：缺少 PE\\0\\0 签名")

    # IMAGE_FILE_HEADER 从 PE\0\0 后开始。
    # +2 是 NumberOfSections，+16 是 SizeOfOptionalHeader。
    number_of_sections = read_u16(raw, pe_offset + 6)
    optional_size = read_u16(raw, pe_offset + 20)

    # Optional Header 紧跟在 24 字节 PE+COFF 头之后。
    optional = pe_offset + 24
    magic = read_u16(raw, optional)
    if magic != 0x10B:
        raise ValueError("当前扫描器只支持 32 位 PE32（Magic 0x10B）")

    # PE32 Optional Header 中：
    # +28 = ImageBase
    # +56 = SizeOfImage
    # +60 = SizeOfHeaders
    image_base = read_u32(raw, optional + 28)
    size_of_image = read_u32(raw, optional + 56)
    size_of_headers = read_u32(raw, optional + 60)

    # 创建一个和 Windows 映像大小一样的 bytearray。
    # bytearray 是“可修改字节数组”，方便把各节复制到自己的 RVA。
    image = bytearray(size_of_image)

    # PE 头本身也属于映像的一部分，先复制头。
    header_copy = min(size_of_headers, len(raw), len(image))
    image[:header_copy] = raw[:header_copy]

    # Section Table 位于 Optional Header 之后，每个 IMAGE_SECTION_HEADER 40 字节。
    section_table = optional + optional_size
    sections = []

    for index in range(number_of_sections):
        entry = section_table + index * 40

        # 前 8 字节是节名，例如 .text、.rdata、.patch。
        name = raw[entry:entry + 8].split(b"\0", 1)[0].decode("latin1")

        # Section Header +8 开始依次是：
        # VirtualSize、VirtualAddress、SizeOfRawData、PointerToRawData。
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from(
            "<IIII", raw, entry + 8
        )
        characteristics = read_u32(raw, entry + 36)

        # 真正能复制多少字节要同时受三个条件限制：
        # 1. 节在磁盘里声明的 RawSize；
        # 2. 文件剩余长度；
        # 3. 映像目标区剩余长度。
        copy_size = min(
            raw_size,
            max(0, len(raw) - raw_pointer),
            max(0, size_of_image - virtual_address),
        )

        if copy_size > 0:
            image[virtual_address:virtual_address + copy_size] = raw[
                raw_pointer:raw_pointer + copy_size
            ]

        sections.append(
            {
                "name": name,
                "rva": virtual_address,
                "virtual_size": virtual_size,
                "raw_size": raw_size,
                "raw_pointer": raw_pointer,
                "characteristics": characteristics,
            }
        )

    return image_base, image, sections


# ---------------------------------------------------------------------------
# 简单的“找字节串 / 找字符串”工具
# ---------------------------------------------------------------------------

def find_all_bytes(image: bytes | bytearray, needle: bytes):
    """返回 needle 在 image 中每一次出现的偏移。"""
    hits = []
    start = 0
    while True:
        found = image.find(needle, start)
        if found < 0:
            break
        hits.append(found)
        # +1 而不是 +len(needle)，这样即使重叠出现也不会漏掉。
        start = found + 1
    return hits


def find_ascii_string(image_base: int, image: bytearray, text: str):
    """寻找完整的 NUL 结尾 ASCII 字符串，返回虚拟地址列表。"""
    needle = text.encode("ascii") + b"\0"
    return [image_base + offset for offset in find_all_bytes(image, needle)]


def image_ascii_equals(image_base: int, image: bytearray, va: int, text: str) -> bool:
    """检查某个虚拟地址上是否正好放着指定 NUL 结尾 ASCII 字符串。"""
    offset = va - image_base
    needle = text.encode("ascii") + b"\0"
    if offset < 0 or offset + len(needle) > len(image):
        return False
    return image[offset:offset + len(needle)] == needle


# ---------------------------------------------------------------------------
# SteamFix 语义结构 1：LoadLibraryA(DLL) + 错误的 test ecx,ecx
# ---------------------------------------------------------------------------
# 我们寻找的不是裸 85 C9，而是整个结构：
#
#   8D 0D <字符串绝对地址>    lea ecx, [dllName]
#   51                       push ecx
#   FF 15 <IAT槽地址>         call dword ptr [LoadLibraryA]
#   85 C9                    test ecx, ecx      <- Steam 移植错误
#
# 这样能大幅降低误命中概率。

def find_load_library_site(
    image_base: int,
    image: bytearray,
    dll_name: str,
    expected_iat: int = 0,
):
    hits = []

    # 这个模式至少需要 15 字节，所以最后 14 字节不用再尝试。
    for offset in range(0, len(image) - 15 + 1):
        q = image[offset:offset + 15]

        # 8D 0D = lea ecx, [absolute-address]
        if q[0:2] != b"\x8D\x0D":
            continue

        string_va = read_u32(q, 2)
        if not image_ascii_equals(image_base, image, string_va, dll_name):
            continue

        # 51 FF 15 = push ecx ; call dword ptr [absolute]
        if q[6:9] != b"\x51\xFF\x15":
            continue

        iat_va = read_u32(q, 9)
        if expected_iat and iat_va != expected_iat:
            continue

        # 85 C9 = test ecx, ecx。
        if q[13:15] != b"\x85\xC9":
            continue

        hits.append((image_base + offset, iat_va))

    return hits


# ---------------------------------------------------------------------------
# SteamFix 语义结构 2：根据函数名字推导 BaldrUtil 导出函数槽
# ---------------------------------------------------------------------------
# Steam 补丁使用类似：
#
#   68 <slot地址>             push slot
#   68 <"UpdateSteam"地址>   push name
#   E8 <rel32>               call GetProcAddress-helper
#
# helper 会把 GetProcAddress 的返回值写进 slot。
# 因此，只要函数名唯一，就能反推出该导出的运行时函数指针槽。

def find_proc_slot(image_base: int, image: bytearray, function_name_va: int):
    hits = []

    for offset in range(0, len(image) - 15 + 1):
        if image[offset] != 0x68:
            continue
        if image[offset + 5] != 0x68:
            continue
        if image[offset + 10] != 0xE8:
            continue

        if read_u32(image, offset + 6) != function_name_va:
            continue

        slot_va = read_u32(image, offset + 1)
        if not (image_base <= slot_va <= image_base + len(image) - 4):
            continue

        rel = read_s32(image, offset + 11)
        helper_va = image_base + offset + 15 + rel
        if not (image_base <= helper_va < image_base + len(image)):
            continue

        hits.append((image_base + offset, slot_va, helper_va))

    return hits


# ---------------------------------------------------------------------------
# SteamFix 语义结构 3：找 Steam 初始化入口
# ---------------------------------------------------------------------------
# 中文版的初始化入口很短，但英文版在同一入口里还会做自定义字体、Dive1、
# DynamicStringZone 等初始化，所以这里不硬编码“第几字节必须是什么”。
#
# 我们只要求：
#   A. 这个候选函数通过 E8 调到了已经确定的 loader helper；
#   B. 在后面最多 192 字节里，通过 FF 15 调到了 InitSteam 槽；
#   C. 这段范围里能看到 ret（C3）。
#
# 最后仍然要求整个映像只有 1 个候选。

def find_init_entry(
    image_base: int,
    image: bytearray,
    loader_helper_va: int,
    init_steam_slot_va: int,
):
    hits = []

    for offset in range(0, len(image) - 5):
        if image[offset] != 0xE8:
            continue

        rel = read_s32(image, offset + 1)
        target = image_base + offset + 5 + rel
        if target != loader_helper_va:
            continue

        window = min(192, len(image) - offset)
        saw_init_call = False
        saw_ret = False

        for k in range(5, window):
            if image[offset + k] == 0xC3:
                saw_ret = True

            if (
                k + 6 <= window
                and image[offset + k:offset + k + 2] == b"\xFF\x15"
                and read_u32(image, offset + k + 2) == init_steam_slot_va
            ):
                saw_init_call = True

        if saw_init_call and saw_ret:
            hits.append(image_base + offset)

    return hits


# ---------------------------------------------------------------------------
# SteamFix 语义结构 4：找每帧 UpdateSteam trampoline
# ---------------------------------------------------------------------------
# 已确认中英文版都使用：
#
#   push eax
#   push ebx
#   push ecx
#   push edx
#   call [UpdateSteamSlot]
#   pop edx
#   pop ecx
#   pop ebx
#   pop eax
#   push esi
#   mov esi,ecx
#   call rel32
#   jmp  rel32
#
# 最后一条 jmp 的旧目标比真正指令边界早 3 字节。
# 原因：trampoline 重放了 8 字节原指令，但旧 detour 只按 5 字节覆盖长度续接。
# 所以新目标可以从旧目标直接计算：old + (8 - 5) = old + 3。

def find_update_trampoline(image_base: int, image: bytearray, update_slot_va: int):
    hits = []

    for offset in range(0, len(image) - 27 + 1):
        if image[offset:offset + 4] != b"\x50\x53\x51\x52":
            continue
        if image[offset + 4:offset + 6] != b"\xFF\x15":
            continue
        if read_u32(image, offset + 6) != update_slot_va:
            continue
        if image[offset + 10:offset + 14] != b"\x5A\x59\x5B\x58":
            continue
        if image[offset + 14:offset + 17] != b"\x56\x89\xCE":
            continue
        if image[offset + 17] != 0xE8:
            continue
        if image[offset + 22] != 0xE9:
            continue

        jmp_va = image_base + offset + 22
        old_target = image_base + offset + 27 + read_s32(image, offset + 23)
        new_target = old_target + 3
        hits.append((image_base + offset, jmp_va, old_target, new_target))

    return hits


# ---------------------------------------------------------------------------
# 扫描一个 EXE，并生成便于人类阅读的报告
# ---------------------------------------------------------------------------

def hex_list(values):
    return [f"0x{x:08X}" for x in values]


def scan_exe(exe_path: Path) -> tuple[bool, str]:
    image_base, image, sections = map_pe_image(exe_path)

    lines = []
    lines.append(f"文件：{exe_path}")
    lines.append(f"ImageBase：0x{image_base:08X}")
    lines.append(f"SizeOfImage：0x{len(image):08X}")

    # 把 .patch 信息也打印出来，便于检查中英文版布局差异。
    patch_sections = [s for s in sections if s["name"] == ".patch"]
    for section in patch_sections:
        lines.append(
            ".patch："
            f"RVA=0x{section['rva']:08X} "
            f"VirtualSize=0x{section['virtual_size']:X} "
            f"RawSize=0x{section['raw_size']:X} "
            f"Characteristics=0x{section['characteristics']:08X}"
        )

    required_names = [
        "BaldrUtil.dll",
        "InitSteam",
        "CloseSteam",
        "UpdateSteam",
        "ResetAchievements",
        "GetAchievement",
    ]

    string_vas = {}
    all_unique = True

    lines.append("")
    lines.append("[1] 独立语义字符串")
    for name in required_names:
        hits = find_ascii_string(image_base, image, name)
        lines.append(f"  {name}: matches={len(hits)} addresses={hex_list(hits)}")
        if len(hits) == 1:
            string_vas[name] = hits[0]
        else:
            string_vas[name] = 0
            all_unique = False

    lines.append("")
    lines.append("[2] 两次 LoadLibraryA 结构")

    baldr_load = find_load_library_site(image_base, image, "BaldrUtil.dll")
    lines.append(
        "  BaldrUtil.dll load: "
        f"matches={len(baldr_load)} "
        f"data={[ (f'0x{a:08X}', f'0x{i:08X}') for a, i in baldr_load ]}"
    )

    load_iat = baldr_load[0][1] if len(baldr_load) == 1 else 0
    gdi_load = (
        find_load_library_site(image_base, image, "GDI32.dll", load_iat)
        if load_iat
        else []
    )
    lines.append(
        "  GDI32.dll load: "
        f"matches={len(gdi_load)} "
        f"data={[ (f'0x{a:08X}', f'0x{i:08X}') for a, i in gdi_load ]}"
    )

    lines.append("")
    lines.append("[3] BaldrUtil 导出函数槽")

    init_slots = (
        find_proc_slot(image_base, image, string_vas["InitSteam"])
        if string_vas["InitSteam"]
        else []
    )
    update_slots = (
        find_proc_slot(image_base, image, string_vas["UpdateSteam"])
        if string_vas["UpdateSteam"]
        else []
    )

    lines.append(
        "  InitSteam: "
        f"matches={len(init_slots)} "
        f"data={[tuple(f'0x{x:08X}' for x in row) for row in init_slots]}"
    )
    lines.append(
        "  UpdateSteam: "
        f"matches={len(update_slots)} "
        f"data={[tuple(f'0x{x:08X}' for x in row) for row in update_slots]}"
    )

    # loader helper 固定在 GDI LoadLibrary 结构前 7 字节。
    loader_helper = gdi_load[0][0] - 7 if len(gdi_load) == 1 else 0
    helper_bytes = b""
    if loader_helper:
        helper_offset = loader_helper - image_base
        helper_bytes = bytes(image[helper_offset:helper_offset + 7])
    lines.append(
        "  loader helper: "
        f"address={'0x%08X' % loader_helper if loader_helper else 'N/A'} "
        f"bytes={helper_bytes.hex(' ') if helper_bytes else 'N/A'}"
    )

    init_slot_va = init_slots[0][1] if len(init_slots) == 1 else 0
    update_slot_va = update_slots[0][1] if len(update_slots) == 1 else 0

    lines.append("")
    lines.append("[4] Steam 初始化入口")
    init_entries = (
        find_init_entry(image_base, image, loader_helper, init_slot_va)
        if loader_helper and init_slot_va
        else []
    )
    lines.append(
        f"  matches={len(init_entries)} addresses={hex_list(init_entries)}"
    )

    lines.append("")
    lines.append("[5] UpdateSteam trampoline")
    trampolines = (
        find_update_trampoline(image_base, image, update_slot_va)
        if update_slot_va
        else []
    )
    for row in trampolines:
        start, jmp_va, old_target, new_target = row
        lines.append(
            "  "
            f"trampoline=0x{start:08X} "
            f"jmp=0x{jmp_va:08X} "
            f"oldTarget=0x{old_target:08X} "
            f"newTarget=0x{new_target:08X}"
        )
    if not trampolines:
        lines.append("  matches=0")

    # 这里把所有“必须唯一”的条件集中成最终 PASS 判定。
    # 任意一项不是 1 都必须 FAIL，绝不能“拿第一处试试看”。
    pass_conditions = [
        all_unique,
        len(baldr_load) == 1,
        len(gdi_load) == 1,
        len(init_slots) == 1,
        len(update_slots) == 1,
        len(init_entries) == 1,
        len(trampolines) == 1,
    ]

    # InitSteam 和 UpdateSteam 必须使用同一个 GetProcAddress helper。
    if len(init_slots) == 1 and len(update_slots) == 1:
        same_helper = init_slots[0][2] == update_slots[0][2]
    else:
        same_helper = False
    pass_conditions.append(same_helper)

    # GDI32 与 BaldrUtil 必须使用同一个 LoadLibraryA IAT 槽。
    if len(baldr_load) == 1 and len(gdi_load) == 1:
        same_loadlibrary = baldr_load[0][1] == gdi_load[0][1]
    else:
        same_loadlibrary = False
    pass_conditions.append(same_loadlibrary)

    passed = all(pass_conditions)
    lines.append("")
    lines.append("最终结果：" + ("PASS" if passed else "FAIL"))

    return passed, "\n".join(lines)


# ---------------------------------------------------------------------------
# 命令行入口
# ---------------------------------------------------------------------------
# 用法：
#   python signature_scanner.py BaldrSky.exe
#   python signature_scanner.py CN.exe EN.exe
#
# 多个文件会逐个扫描；只要其中一个 FAIL，整个进程退出码就是 1。

def main() -> int:
    if len(sys.argv) < 2:
        print("用法：python signature_scanner.py <BaldrSky.exe> [更多EXE...]")
        return 2

    overall_ok = True

    for arg in sys.argv[1:]:
        path = Path(arg)
        print("=" * 78)
        try:
            passed, report = scan_exe(path)
            print(report)
            if not passed:
                overall_ok = False
        except Exception as exc:
            overall_ok = False
            print(f"文件：{path}")
            print(f"最终结果：ERROR - {exc}")

    return 0 if overall_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
