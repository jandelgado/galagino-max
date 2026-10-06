#!/usr/bin/env python3
"""The Pit ROM converter for galagino (MAME set thepit)."""

import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join("..", "pyconv"))
from asset_emit import emit_compressed
from gfxutil import load_file, mame_decode, rot_galagino

ROM_SET = os.path.normpath(os.path.join("..", "..", "..", "romszip", "thepit.zip"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "..", "source", "src", "machines", "thepit"))

ARENA_CAPACITY = 182000


def step8(start, step):
    return [start + i * step for i in range(8)]


def load(name, sha1):
    return load_file(ROM_SET, [name], sha1)


# MAME gfx1: pit8 at 0x0000, pit7 at 0x1000, planes { 0x1000*8, 0 }.
# Chars and sprites decode the same bytes.
def gfx_region(pit7, pit8):
    region = bytearray(0x1800)
    region[0x0000:0x0800] = pit8
    region[0x1000:0x1800] = pit7
    return region


PLANES = [0x1000 * 8, 0]


def decode_chars(region):
    return mame_decode(region, 8, 8, PLANES, step8(0, 1), step8(0, 8), 8 * 8, 256)


def decode_sprites(region):
    xoffs = step8(0, 1) + step8(8 * 8, 1)
    yoffs = step8(0, 8) + step8(16 * 8, 8)
    return mame_decode(region, 16, 16, PLANES, xoffs, yoffs, 32 * 8, 64)


# One word per galagino line, pixel 0 (leftmost column) in the low 2 bits.
def pack_rows(tile):
    rows = []
    for row in rot_galagino(tile):
        v = 0
        for x, pen in enumerate(row):
            v |= pen << (2 * x)
        rows.append(v)
    return rows


def rgb565_swapped(r, g, b):
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return ((v & 0xFF) << 8) | (v >> 8)


def bit(v, n):
    return (v >> n) & 1


# MAME thepit_palette: 32 PROM colors, then 8 primary colors used by the
# solid background layer (pens 32-39).
def convert_palette(prom):
    pal = []
    for v in prom[:32]:
        r = 0x21 * bit(v, 0) + 0x47 * bit(v, 1) + 0x97 * bit(v, 2)
        g = 0x21 * bit(v, 3) + 0x47 * bit(v, 4) + 0x97 * bit(v, 5)
        b = 0x47 * bit(v, 6) + 0x97 * bit(v, 7)
        pal.append(rgb565_swapped(r, g, b))
    for i in range(8):
        pal.append(rgb565_swapped(0xFF * bit(i, 2), 0xFF * bit(i, 1), 0xFF * bit(i, 0)))
    return pal


def write(name, ctype, dims, count, flat, comment, elem_size):
    path = os.path.join(OUT_DIR, name + ".h")
    with open(path, "w") as f:
        f.write(f"// {comment}\n")
        emit_compressed(f, name, ctype, dims, count, flat)
    size = len(flat) * elem_size
    print(f"Wrote: {os.path.abspath(path)} ({size} bytes resident)")
    return size


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    main_rom = bytearray()
    for name, sha1 in [
        ("pit1.bin", "e64cb2f8d546f5d44dc10a4178f3d211882c45a9"),
        ("pit2.bin", "772ff81cf44d21981f9768f017af5cb81ff57be3"),
        ("pit3.bin", "8991c6424f008ddd15edac953635aecdba4ea696"),
        ("pit4.bin", "1dd9ded69121d28b674209abe1efd60afde6926d"),
        ("pit5.bin", "bb5133784b2b6a5c217d9ea42641daba2a797fff"),
    ]:
        main_rom += load(name, sha1)
    sound_rom = load("pit6.bin", "ba78b035a91a67732414ba327640fb771d4323c5")
    pit7 = load("pit7.bin", "4a35dd857ca352e0260361376fe666af4b3315af")
    pit8 = load("pit8.bin", "9baf094baab8325af659879cfb6984eeca0d94bd")
    prom = load("82s123.ic4", "d188c90dba10fe3abaae92488786b555b35218c5")

    region = gfx_region(pit7, pit8)
    tiles = [v for t in decode_chars(region) for v in pack_rows(t)]
    sprites = [v for s in decode_sprites(region) for v in pack_rows(s)]
    cmap = convert_palette(prom)

    total = 0
    total += write("thepit_main_rom", "unsigned char", "", len(main_rom), list(main_rom),
                   "The Pit main CPU ROM", 1)
    total += write("thepit_sound_rom", "unsigned char", "", len(sound_rom), list(sound_rom),
                   "The Pit sound CPU ROM", 1)
    total += write("thepit_tiles", "unsigned short", "[8]", 256, tiles,
                   "The Pit chars: 256 x 8x8 2bpp, rotated, pixel 0 in bits 0-1", 2)
    total += write("thepit_sprites", "uint32_t", "[16]", 64, sprites,
                   "The Pit sprites: 64 x 16x16 2bpp, rotated, unflipped", 4)
    total += write("thepit_cmap", "unsigned short", "[4]", len(cmap) // 4, cmap,
                   "The Pit colors: rows 0-7 PROM palettes, rows 8-9 background 0-7", 2)

    print(f"Arena budget: {total} / {ARENA_CAPACITY} bytes")
    if total >= ARENA_CAPACITY:
        sys.exit("Arena budget exceeded")


if __name__ == "__main__":
    main()
