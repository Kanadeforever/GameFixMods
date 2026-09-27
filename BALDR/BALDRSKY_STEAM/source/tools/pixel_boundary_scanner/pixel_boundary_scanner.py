#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BALDR SKY 字体像素左边界故障离线扫描工具（test14；算法自 test12 起沿用）。

这个工具只读，不会修改游戏文件。

它验证三件事必须同时唯一成立：
1. 像素混合故障点：movzx edx,[ebx] / mov ecx,[edi]；
2. 故障点前的目标对象来源：mov edx,[esp+XX] / mov edi,[edx+4]；
3. 故障点后的安全续接：mov [edi],ebx / add edi,4。

这样 DLL 才能在不写死中文版地址、也不写死 0x38 栈偏移的情况下，
自动构造 test12 起沿用至 test14 的“异常发生前内联边界检查”。
"""

from pathlib import Path
import struct
import sys


def read_u16(data: bytes, off: int) -> int:
    """按小端读取 16 位整数。"""
    return struct.unpack_from("<H", data, off)[0]


def read_u32(data: bytes, off: int) -> int:
    """按小端读取 32 位整数。"""
    return struct.unpack_from("<I", data, off)[0]


def parse_pe_sections(data: bytes):
    """读取最少量 PE32 头信息，返回 ImageBase 与节表。"""
    if len(data) < 0x100 or data[:2] != b"MZ":
        raise ValueError("不是有效 PE：缺少 MZ。")
    pe_off = read_u32(data, 0x3C)
    if pe_off + 24 > len(data) or data[pe_off:pe_off + 4] != b"PE\0\0":
        raise ValueError("不是有效 PE：缺少 PE\\0\\0。")
    number_of_sections = read_u16(data, pe_off + 6)
    optional_size = read_u16(data, pe_off + 20)
    optional = pe_off + 24
    if read_u16(data, optional) != 0x10B:
        raise ValueError("只支持 PE32/x86。")
    image_base = read_u32(data, optional + 28)
    section_table = optional + optional_size
    sections = []
    for index in range(number_of_sections):
        off = section_table + index * 40
        raw_name = data[off:off + 8].split(b"\0", 1)[0]
        name = raw_name.decode("ascii", errors="replace")
        virtual_size = read_u32(data, off + 8)
        virtual_address = read_u32(data, off + 12)
        raw_size = read_u32(data, off + 16)
        raw_pointer = read_u32(data, off + 20)
        sections.append((name, virtual_address, virtual_size, raw_pointer, raw_size))
    return image_base, sections


def raw_offset_to_va(image_base: int, sections, raw_off: int):
    """把文件偏移转换成运行时 VA。"""
    for name, va, virtual_size, raw_pointer, raw_size in sections:
        span = max(virtual_size, raw_size)
        if raw_pointer <= raw_off < raw_pointer + span:
            return image_base + va + (raw_off - raw_pointer), name
    return None, None


def find_all(data: bytes, needle: bytes):
    """返回 needle 的全部命中位置。"""
    hits = []
    start = 0
    while True:
        pos = data.find(needle, start)
        if pos < 0:
            return hits
        hits.append(pos)
        start = pos + 1


def main() -> int:
    if len(sys.argv) != 2:
        print("用法：python pixel_boundary_scanner.py BaldrSky.bin")
        return 2

    path = Path(sys.argv[1])
    data = path.read_bytes()
    image_base, sections = parse_pe_sections(data)

    signature = bytes.fromhex(
        "0F B7 13 "
        "8B 0F "
        "81 E2 00 FF FF FF "
        "C1 E2 10 "
        "3B D1 "
        "76"
    )
    hits = find_all(data, signature)

    print(f"文件：{path}")
    print(f"像素混合核心特征命中数：{len(hits)}")

    valid = []
    for raw_off in hits:
        sig_va, sec_name = raw_offset_to_va(image_base, sections, raw_off)
        fault_raw = raw_off + 3
        fault_va, _ = raw_offset_to_va(image_base, sections, fault_raw)

        # 与 DLL 一样，在前 0x100 字节找：
        #   8B 54 24 XX   mov edx,[esp+XX]
        #   8B 7A 04      mov edi,[edx+4]
        # XX 就是目标绘图对象在当前函数栈上的保存位置。
        back_start = max(0, raw_off - 0x100)
        base_load_hits = []
        for k in range(back_start, raw_off - 6):
            t = data[k:k + 7]
            if len(t) == 7 and t[0:3] == b"\x8B\x54\x24" and t[4:7] == b"\x8B\x7A\x04":
                base_load_hits.append((k, t[3]))

        # 后 0x100 字节找写像素 + 目标指针推进。
        window = data[fault_raw:fault_raw + 0x100]
        local_hits = find_all(window, bytes.fromhex("89 1F 83 C7 04"))

        print(
            f"- signature raw=0x{raw_off:08X} VA=0x{sig_va:08X} "
            f"section={sec_name} fault=0x{fault_va:08X} "
            f"目标基址来源候选={len(base_load_hits)} 安全续接候选={len(local_hits)}"
        )

        if len(base_load_hits) != 1 or len(local_hits) != 1:
            continue

        base_raw, stack_offset = base_load_hits[0]
        base_va, _ = raw_offset_to_va(image_base, sections, base_raw)
        skip_raw = fault_raw + local_hits[0] + 2
        skip_va, _ = raw_offset_to_va(image_base, sections, skip_raw)
        return_va, _ = raw_offset_to_va(image_base, sections, raw_off + 5)

        print(f"  目标对象来源：0x{base_va:08X}；栈偏移=0x{stack_offset:02X}")
        print(f"  正常返回点：0x{return_va:08X}")
        print(f"  安全续接点：0x{skip_va:08X}")
        valid.append((sig_va, fault_va, return_va, skip_va, stack_offset))

    if len(valid) != 1:
        print("结果：FAIL——不是唯一完整结构，DLL 也必须拒绝内联修改。")
        return 1

    sig_va, fault_va, return_va, skip_va, stack_offset = valid[0]
    print("结果：PASS")
    print(f"内联补丁点：0x{sig_va:08X}")
    print(f"故障读取点：0x{fault_va:08X}")
    print(f"正常返回点：0x{return_va:08X}")
    print(f"越界像素续接：0x{skip_va:08X}")
    print(f"目标对象栈偏移：0x{stack_offset:02X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
