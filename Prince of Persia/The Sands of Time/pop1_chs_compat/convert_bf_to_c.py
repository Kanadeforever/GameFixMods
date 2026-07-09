"""Convert both BF files to C arrays for MSVC."""
import os

dir = r"c:\Project\GameDecomp\POP_NCN\workspace\src\window_milestone"
out_path = os.path.join(dir, "popdata_bf.c")

for bf_file, start_name, end_name, seg in [
    ("POPData_chs.BF", "g_popdata_chs_start", "g_popdata_chs_end", ".rdA"),
    ("POPData_mixed_gamepad.BF", "g_popdata_mixed_start", "g_popdata_mixed_end", ".rdB"),
]:
    bf_path = os.path.join(dir, bf_file)
    if not os.path.exists(bf_path):
        print(f"WARNING: {bf_file} not found")
        continue

    data = open(bf_path, 'rb').read()
    with open(out_path, 'a' if os.path.exists(out_path) else 'w') as f:
        if not os.path.exists(out_path) or os.path.getsize(out_path) == 0:
            f.write("/* Auto-generated from POPData BFs - window_milestone */\n\n")
        f.write(f'#pragma data_seg("{seg}")\n')
        f.write(f'__declspec(allocate("{seg}")) const unsigned char {start_name}[] = {{\n')
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            f.write(f"  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",\n")
        f.write("};\n")
        f.write(f'__declspec(allocate("{seg}")) const unsigned char {end_name}[] = {{ 0 }};\n')
        f.write('#pragma data_seg()\n\n')
        print(f"{bf_file}: {len(data):,} bytes -> {seg}")

print(f"Done: {out_path}")
