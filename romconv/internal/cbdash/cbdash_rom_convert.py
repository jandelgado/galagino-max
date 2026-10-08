#!/usr/bin/env python3
# Boulder Dash (Data East 1985, DECO Cassette, set "cbdash", BIOS B).
#
# The tape is not emulated. cbdash.cas is a list of 256-byte blocks; the
# BIOS drops byte 0 of each block (a repeat of byte 1) and copies bytes
# 1..255. Block 0 is a header, blocks 1..90 go to main RAM 0x0500, blocks
# 91..102 are sent to sound RAM 0x0200, block 103 is a trailer. Verified against a MAME tape boot.
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join("..", "pyconv"))
from gfxutil import load_file
from asset_emit import emit_compressed

ROMS = os.path.normpath(os.path.join("..", "..", "..", "romszip"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "..", "source", "src", "machines", "cbdash"))

CAS = (["cbdash.cas"], "5d163d8e31c58b20679c6be06b1aa02df621822b")
BIOS = (["v0b-.7e"], "063f83020ba3d6f43ab8471f95ca919767b93aa4")
AUDIO = (["v1-.5a"], "0097f38beb4872e735e560148052e258a26b08fd")

def blocks(cas, first, last):
    return b"".join(cas[b * 256 + 1:b * 256 + 256] for b in range(first, last + 1))

def write(name, sym, data, comment):
    with open(os.path.join(OUT_DIR, name), "w") as f:
        print(f"// {comment}", file=f)
        emit_compressed(f, sym, "unsigned char", "", len(data), list(data))
    print(f"{sym}: {len(data)} bytes")
    return len(data)

def main():
    cas = load_file(os.path.join(ROMS, "cbdash.zip"), *CAS)
    bios = load_file(os.path.join(ROMS, "decocass.zip"), *BIOS)
    audio = load_file(os.path.join(ROMS, "decocass.zip"), *AUDIO)

    main_img = blocks(cas, 1, 90)
    sound_img = blocks(cas, 91, 102)
    assert len(main_img) == 22950 and len(sound_img) == 3060
    # block 103 is an "EOB001" trailer the BIOS never reads
    assert cas[103 * 256 + 1:103 * 256 + 4] == b"EOB" and not any(cas[104 * 256:])

    total = write("cbdash_rom_main.h", "cbdash_rom_main", main_img,
                  "main RAM image, load at 0x0500, entry 0x0503")
    total += write("cbdash_rom_sound.h", "cbdash_rom_sound", sound_img,
                   "sound RAM image, load at 0x0200, entry 0x0203")
    total += write("cbdash_rom_bios.h", "cbdash_rom_bios", bios,
                   "BIOS B, main CPU 0xf000-0xffff, encrypted opcodes")
    total += write("cbdash_rom_audio.h", "cbdash_rom_audio", audio,
                   "sound CPU ROM 0xf800-0xffff")
    print(f"Arena (decompressed assets): {total} bytes")

if __name__ == "__main__":
    main()
