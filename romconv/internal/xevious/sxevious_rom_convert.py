#!/usr/bin/env python3
# Super Xevious (sxevious): only the main and sub CPU ROMs differ from
# xevious (MAME galaga.cpp ROM_START(sxevious), CRCs checked 2026-10-14).
# Every other asset comes from the xevious conversion.
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join("..", "pyconv"))
from gfxutil import load_file
from asset_emit import emit_compressed

ROM_SET = os.path.normpath(os.path.join("..", "..", "..", "romszip", "sxevious.zip"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "..", "source", "src", "machines", "sxevious"))

CPU1 = [
    ("cpu_3p.rom", "2c41303d8c74acb5840295a4b460a39a9a8e21bb"),
    ("cpu_3m.rom", "7169e7f3bd1e9cfae9671b89f2a45f56b968e1ff"),
    ("xv3_3.2m",   "ecc39fb2c0065a36f20541747089b4e30dfb99b1"),
    ("xv3_4.2l",   "0ca726f7f9528789f2a718df55e59406a283cdfa"),
]
CPU2 = [
    ("xv3_5.3f", "5831bb306bd650779207936bfd00f25864733abb"),
    ("xv3_6.3j", "5a020822387ab8c69214db961180760fa9853e6e"),
]


def write_rom(sym, parts, comment):
    data = b"".join(load_file(ROM_SET, [name], sha1) for name, sha1 in parts)
    with open(os.path.join(OUT_DIR, sym + ".h"), "w") as f:
        print(f"// {comment}", file=f)
        emit_compressed(f, sym, "unsigned char", "", len(data), list(data))
    return len(data)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    total = write_rom("sxevious_rom_cpu1", CPU1, "Super Xevious CPU1 (main) Z80 ROM 0x0000-0x3FFF")
    total += write_rom("sxevious_rom_cpu2", CPU2, "Super Xevious CPU2 (motion) Z80 ROM 0x0000-0x1FFF")
    print(f"sxevious own resident assets: {total} B (rest shared with xevious)")


if __name__ == "__main__":
    main()
