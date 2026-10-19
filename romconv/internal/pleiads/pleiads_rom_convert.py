#!/usr/bin/env python3
# Pleiads (Tehkan 1981), MAME phoenix/phoenix.cpp, set pleiads: Phoenix
# board, ROT90. Tiles stored per native x column for the ROT90 line
# render: uint16_t[8], word[x] bits 2y = pixel at native y.
# Palette: 256 RGB565, pen = bank << 6 | fg << 5 | (code >> 5) << 2 | p,
# MAME pleiads_palette (bitswap 7,6,5,1,0,4,3,2), colors as MAME 0.289.
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join("..", "pyconv"))
from gfxutil import load_file, mame_decode, rgb888_to_rgb565_le
from asset_emit import emit_compressed

ROM_SET = os.path.normpath(os.path.join("..", "..", "..", "romszip", "pleiads.zip"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "..", "source", "src", "machines", "pleiads"))

if not os.path.isfile(ROM_SET):
    print(f"ERROR: missing {ROM_SET}", file=sys.stderr)
    sys.exit(1)
os.makedirs(OUT_DIR, exist_ok=True)


def rom(name, sha1):
    return load_file(ROM_SET, [name], sha1)


main = b"".join(rom(n, s) for n, s in [
    ("ic47.r1", "52a3232e99920805ce9e195b8a6338ae7044dd18"),
    ("ic48.r2", "312a33cca09d5d2d18992f28eb051230a90db6e3"),
    ("ic47.bin", "0f352b5461da957c564920fd1da83bc81f41ffb9"),
    ("ic48.bin", "6bfdc6c965199c5d4d687fe35dda057ec38cd8e0"),
    ("ic51.r5", "fd7937d0c114c8d9c1efaa9918ae3df2af41f032"),
    ("ic50.bin", "5c183e3a73fa882ffec3cb9219fb5619e625591a"),
    ("ic53.r7", "2ae687c84732942e69ad4dfb7a4ac1b97b77487a"),
    ("ic52.bin", "7e4ef298c8ddefc7dc0cbf94a9c9f36a4b807ba0")])
# The Z80 core runs this 8085 code; 8085 SIM (0x30, SOD output, not wired
# in MAME) would run as Z80 JR NC and jump into the sound latch writer at
# 27ad. The two SIMs in code (MAME disassembly; 27e3 executes every frame,
# 03f2 on the init path) become NOP. No 8085 RIM executes (host and MAME
# fetch traces, attract + gameplay).
SIM, NOP = 0x30, 0x00
main = bytearray(main)
for addr in (0x03F2, 0x27E3):
    assert main[addr] == SIM, f"no SIM at {addr:04x}"
    main[addr] = NOP
main = bytes(main)

bg = rom("ic23.bin", "da023a94725dc40107cd97e4decfd4dc0f9f00ee") + rom("ic24.bin", "421dedc674c6dde7abf01412df035a8eb8e6db9b")
fg = rom("ic39.bin", "cd240bd056f761b2f9e2142049434f02cae3e315") + rom("ic40.bin", "8349008ab1d8ef08775b54170c37deb1d391fffc")
prom_lo = rom("7611-5.33", "252880d80425b2e697146e76efdc6cb9f3ba0378")
prom_hi = rom("7611-5.26", "bdfab316ea26e2063879e7aa78b6ae2b55eb95c8")

TILE_COUNT = 256


def tiles(data):
    dec = mame_decode(data, 8, 8, [256 * 8 * 8, 0], [7, 6, 5, 4, 3, 2, 1, 0],
                      [8 * i for i in range(8)], 8 * 8, TILE_COUNT)
    return [sum(t[y][x] << (2 * y) for y in range(8)) for t in dec for x in range(8)]


# MAME 0.289 pleiads_palette() output per (low PROM, high PROM) nibble pair,
# read from the running driver (Lua palette dump, 2026-10-16). Its resnet
# (open collector, shared bias) couples the channels, e.g. pure red is
# ca1618, so a per-channel level table does not reproduce it. All 256 pens
# of this PROM set use these 15 pairs.
MAME_COLORS = {
    (0b0000, 0b0000): 0x000000, (0b0001, 0b0000): 0x9f1114, (0b0001, 0b0001): 0xca1618,
    (0b0010, 0b0010): 0x0a07bd, (0b0011, 0b0001): 0xd11eaa, (0b0011, 0b0011): 0xd31fd2,
    (0b0100, 0b0100): 0x2edf2d, (0b0101, 0b0100): 0xcdf040, (0b0101, 0b0101): 0xf6f644,
    (0b0110, 0b0000): 0x2ab5b6, (0b0110, 0b0010): 0x2db8e0, (0b0110, 0b0100): 0x35e7bf,
    (0b0110, 0b0110): 0x36e8e9, (0b0111, 0b0100): 0xd4f7d5, (0b0111, 0b0111): 0xffffff,
}


def color(i):
    b = [(i >> n) & 1 for n in range(8)]
    col = b[7] << 7 | b[6] << 6 | b[5] << 5 | b[1] << 4 | b[0] << 3 | b[4] << 2 | b[3] << 1 | b[2]
    c = MAME_COLORS[(prom_lo[col] & 0x0F, prom_hi[col] & 0x0F)]
    return rgb888_to_rgb565_le(c >> 16, (c >> 8) & 0xFF, c & 0xFF)


palette = [color(i) for i in range(256)]


def write(name, fn):
    path = os.path.join(OUT_DIR, name)
    with open(path, "w") as f:
        fn(f)
    print(f"wrote {path}")


write("pleiads_rom.h", lambda f: emit_compressed(f, "pleiads_rom", "unsigned char", "", len(main), main))
write("pleiads_tiles.h", lambda f: (
    emit_compressed(f, "pleiads_bgtiles", "unsigned short", "[8]", TILE_COUNT, tiles(bg)),
    emit_compressed(f, "pleiads_fgtiles", "unsigned short", "[8]", TILE_COUNT, tiles(fg))))
write("pleiads_palette.h", lambda f: emit_compressed(f, "pleiads_palette", "unsigned short", "", 256, palette))

sizes = {"main": len(main), "bg": TILE_COUNT * 16, "fg": TILE_COUNT * 16, "palette": 512}
for k, v in sizes.items():
    print(f"  {k:8s} {v:6d}")
total = sum(sizes.values())
print(f"Arena total {total} B (budget 182000)")
assert total < 182000
