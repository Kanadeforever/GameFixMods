from pathlib import Path
import hashlib
import shutil
import sys

EXPECTED_SHA256 = "afb81b11704cf22bc572ce39b48b1cd046dca1a47ca220b5df77fd02f9b6a5d8"    # 如果SHA256不一致可以改这里
OFFSET = 0x1440
OLD = bytes.fromhex("E8 4B FF FF FF 83")
NEW = bytes.fromhex("B8 01 00 00 00 C3")

def main():
    if len(sys.argv) != 2:
        print("用法: python patch_baldrforce_se_win11_auth.py BaldrForceSE.exe")
        raise SystemExit(2)
    p = Path(sys.argv[1])
    data = bytearray(p.read_bytes())
    sha = hashlib.sha256(data).hexdigest()
    if sha != EXPECTED_SHA256:
        print("拒绝补丁：EXE SHA-256 与已分析版本不一致。")
        print("当前 :", sha)
        print("预期 :", EXPECTED_SHA256)
        raise SystemExit(1)
    if data[OFFSET:OFFSET+len(OLD)] != OLD:
        print("拒绝补丁：目标位置原始字节不匹配。")
        raise SystemExit(1)
    backup = p.with_suffix(p.suffix + ".bak")
    shutil.copy2(p, backup)
    data[OFFSET:OFFSET+len(NEW)] = NEW
    p.write_bytes(data)
    print("补丁完成。")
    print("备份：", backup)
    print("作用：跳过已失效的 reg.exe/comap.dat 启动认证包装函数。")

if __name__ == "__main__":
    main()
