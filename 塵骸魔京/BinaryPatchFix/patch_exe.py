"""Add imm32.dll + fix.dll to CN exe import table. Output to workspace/."""
import struct, os as _os

_BASE = _os.path.dirname(_os.path.abspath(__file__))
IN_EXE  = _os.path.join(_BASE, "..", "..", "塵骸魔京", "NitroSystem.exe")
OUT_EXE = _os.path.join(_BASE, "..", "NitroSystem_patched.exe")

MACKT_VA = 0x46000
IAT_NEXT = 0x16168  # after kernel32(0x16000) + user32(0x16150) IATs
IDT_NEW  = 0x46700
STR_NEW  = 0x46780

NEW_DLLS = [
    ("imm32.dll", "ImmGetContext",  17, 10),
    ("fix.dll",   "FixInit",        10,  8),
]

def patch():
    with open(IN_EXE, 'rb') as f:
        data = bytearray(f.read())

    pe_off = struct.unpack_from('<I', data, 0x3C)[0]
    cur_rva = struct.unpack_from('<I', data, pe_off + 24 + 0x60 + 8)[0]
    assert cur_rva == MACKT_VA, f"Bad IDT: 0x{cur_rva:X}"

    # Clone existing IDT (kernel32 + user32 + null = 60 bytes)
    data[IDT_NEW:IDT_NEW + 40] = data[MACKT_VA:MACKT_VA + 40]

    iat = IAT_NEXT
    st = STR_NEW
    for idx, (dll, func, ibn_sz, dll_sz) in enumerate(NEW_DLLS):
        doff = IDT_NEW + 40 + idx * 20

        ibn = struct.pack('<H', 0) + func.encode() + b'\x00'
        data[st:st + len(ibn)] = ibn
        ibn_rva = st; st += len(ibn)

        dn = dll.encode() + b'\x00'
        data[st:st + len(dn)] = dn
        dll_rva = st; st += len(dn)

        struct.pack_into('<I', data, iat, ibn_rva)
        struct.pack_into('<I', data, iat + 4, 0)
        struct.pack_into('<IIIII', data, doff, iat, 0, 0, dll_rva, iat)
        iat += 8

    # Null terminator after new descs
    for i in range(20):
        data[IDT_NEW + 40 + len(NEW_DLLS) * 20 + i] = 0

    new_size = (2 + len(NEW_DLLS)) * 20
    struct.pack_into('<I', data, pe_off + 24 + 0x60 + 8, IDT_NEW)
    struct.pack_into('<I', data, pe_off + 24 + 0x60 + 12, new_size)
    struct.pack_into('<I', data, pe_off + 24 + 0x40, 0)

    _os.makedirs(_os.path.dirname(OUT_EXE), exist_ok=True)
    with open(OUT_EXE, 'wb') as f:
        f.write(data)
    print(f"Patched: {OUT_EXE}")

if __name__ == '__main__':
    patch()
