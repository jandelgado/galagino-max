#!/usr/bin/env python3
"""Extract H.E.R.O. (Activision 1984, Atari 2600) levels, graphics and tables.

The game is not emulated: galagino reimplements it (see
source/src/machines/hero/HERO.md). Only data blocks are emitted, at their
original 2600 addresses, so the C++ code reads like the disassembly. Code
bytes interleaved with the bank 0 graphics are zeroed. Also emits two
palettes: Stella PAL (default) and TIA NTSC (MAME tia.cpp formula,
HERO_NTSC_COLORS).

Takes the NTSC or the PAL cart. They differ only in colour bytes (and PAL
frame timing in code not emitted), so PAL colours are mapped back to NTSC
and the output is identical for both. The C++ code uses NTSC colours.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "pyconv"))
from gfxutil import check_file, load_file

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..", "..")
ZIP = os.path.join(ROOT, "romszip", "H.E.R.O..zip")
PAL_BIN = os.path.join(ROOT, "romszip", "H.E.R.O.-pal.bin")
OUT_DIR = os.path.join(ROOT, "source", "src", "machines", "hero")

# MAME softlist a2600:hero, F8 bankswitching, 2 x 4 KB
CART = ("H.E.R.O..A26", "282f94817401e3725c622b73a0c05685ce761783")
PAL_CART_SHA1 = "5e4f98210ef5a6682581b60a9fc939fce6b22d97"

# NTSC -> PAL colour hue (high nibble), luminance unchanged; from diffing both carts
NTSC_TO_PAL = {0x1: 0x2, 0x2: 0x4, 0x4: 0x6, 0x5: 0xC, 0x8: 0xB, 0x9: 0xD, 0xA: 0x5, 0xC: 0x5, 0xD: 0x5}
# PAL colour bytes; hue 5 has three NTSC sources, told apart by address
PAL_COLOURS = [(0xDA0D, 0xDA0D), (0xDD00, 0xDDA8), (0xDDB6, 0xDDFF), (0xFF3C, 0xFF3D), (0xFF6A, 0xFF6C),
               (0xFF7B, 0xFF7B)]
PAL_HUE5 = [(0xDDC4, 0xDDD0, 0xA), (0xDDED, 0xDDEE, 0xD)]  # tentacle, rainbow; else 0xC

# (name, bank, first, last) original addresses; bank 0 at $D000, bank 1 at $F000
BLOCKS = [
    ("hero_b0_level", 0, 0xD02A, 0xD078),  # level number, power bonus, miner screen
    ("hero_b0_gfx", 0, 0xD9E2, 0xDFEB),    # PF0 masks, sprites, colours, digits
    ("hero_b1", 1, 0xF6D2, 0xFFEB),        # rooms $F700-$FEFF, level tables $FF00-
]
# code inside the blocks (static trace of the disassembly), zeroed
CODE = [(0xD9F1, 0xDA03), (0xDBB2, 0xDBFB), (0xDC00, 0xDC35), (0xDC56, 0xDC5D), (0xDE00, 0xDE71)]

# MAME tia_ntsc_video_device::init_palette(), phase shift 26.2
NTSC_IQ = [(0.000, 0.000), (0.192, -0.127), (0.241, -0.048), (0.240, 0.040),
           (0.191, 0.121), (0.103, 0.175), (-0.008, 0.196), (-0.116, 0.174),
           (-0.199, 0.118), (-0.243, 0.037), (-0.237, -0.052), (-0.180, -0.129),
           (-0.087, -0.181), (0.021, -0.196), (0.130, -0.169), (0.210, -0.107)]


def ntsc_rgb(index):
    """TIA colour register value >> 1 -> (r, g, b)"""
    i, q = NTSC_IQ[index >> 3]
    y = (index & 7) / 7.0
    out = []
    for v in (y + 0.956 * i + 0.621 * q, y - 0.272 * i - 0.647 * q, y - 1.106 * i + 1.703 * q):
        v = max(v, 0.0) ** 0.9
        out.append(int(255 * min(v, 1.0) + 0.5))
    return out


# Stella PaletteHandler::ourPALPalette, index = colour register >> 1
STELLA_PAL = [
    0x0b0b0b, 0x333333, 0x595959, 0x7b7b7b, 0x999999, 0xb6b6b6, 0xcfcfcf, 0xe6e6e6,
    0x0b0b0b, 0x333333, 0x595959, 0x7b7b7b, 0x999999, 0xb6b6b6, 0xcfcfcf, 0xe6e6e6,
    0x3b2400, 0x664700, 0x8b7000, 0xac9200, 0xc5ae36, 0xdec85e, 0xf7e27f, 0xfff19e,
    0x004500, 0x006f00, 0x3b9200, 0x65b009, 0x85ca3d, 0xa3e364, 0xbffc84, 0xd5ffa5,
    0x590000, 0x802700, 0xa15700, 0xbc7937, 0xd6985f, 0xeeb381, 0xffce9e, 0xffdcbd,
    0x004900, 0x007200, 0x169216, 0x45af45, 0x6bc96b, 0x8be38b, 0xa9fba9, 0xc5ffc5,
    0x640012, 0x890821, 0xa73d4d, 0xc26472, 0xdc8491, 0xf4a3ae, 0xffbeca, 0xffdae0,
    0x003d29, 0x006a48, 0x048e63, 0x3caa84, 0x62c5a2, 0x83dfbe, 0xa1f8d9, 0xbeffe9,
    0x550046, 0x88006e, 0xa5318d, 0xc159aa, 0xda7cc5, 0xf39adf, 0xffb9f3, 0xffd4f6,
    0x003651, 0x005a7d, 0x117e9c, 0x429cb8, 0x68b7d2, 0x88d2eb, 0xa6ebff, 0xc3ffff,
    0x4c007c, 0x75009d, 0x932eb8, 0xaf57d2, 0xca7aeb, 0xe499ff, 0xecb7ff, 0xf3d4ff,
    0x002d83, 0x003ea4, 0x2d65bf, 0x5685da, 0x79a2f2, 0x99bfff, 0xb7dbff, 0xd3f5ff,
    0x220096, 0x5200b6, 0x7538cf, 0x945fe8, 0xb181ff, 0xc5a0ff, 0xd6bdff, 0xe8daff,
    0x00009a, 0x241db6, 0x504ad0, 0x746fe9, 0x928eff, 0xb1adff, 0xcecaff, 0xe9e5ff,
    0x0b0b0b, 0x333333, 0x595959, 0x7b7b7b, 0x999999, 0xb6b6b6, 0xcfcfcf, 0xe6e6e6,
    0x0b0b0b, 0x333333, 0x595959, 0x7b7b7b, 0x999999, 0xb6b6b6, 0xcfcfcf, 0xe6e6e6,
]


def pal_rgb(index):
    """NTSC colour register value >> 1 -> (r, g, b) of its PAL cart colour"""
    hue = NTSC_TO_PAL.get(index >> 3, index >> 3)
    c = STELLA_PAL[(hue << 3) | (index & 7)]
    return [c >> 16, (c >> 8) & 0xFF, c & 0xFF]


def load_cart():
    """NTSC cart, or the PAL cart with its colours mapped back to NTSC"""
    if os.path.exists(ZIP):
        return bytes(load_file(ZIP, [CART[0]], CART[1]))
    rom = bytearray(open(PAL_BIN, "rb").read())
    check_file(os.path.basename(PAL_BIN), rom, PAL_CART_SHA1)
    pal_to_ntsc = {p: n for n, p in NTSC_TO_PAL.items()}

    for first, last in PAL_COLOURS:
        for a in range(first, last + 1):
            o = (a & 0xFFF) + (4096 if a >= 0xF000 else 0)
            hue = rom[o] >> 4
            if hue == 0x5:
                hue = next((h for lo, hi, h in PAL_HUE5 if lo <= a <= hi), 0xC)
            else:
                hue = pal_to_ntsc.get(hue, hue)
            rom[o] = (hue << 4) | (rom[o] & 0x0F)
    return bytes(rom)


def palette(name, rgb):
    cols = [rgb565_swapped(*rgb(i)) for i in range(128)]
    body = ",\n".join("  " + ", ".join(f"0x{c:04x}" for c in cols[i:i + 8]) for i in range(0, 128, 8))
    return f"static const uint16_t {name}[128] = {{\n{body}\n}};"


def rgb565_swapped(r, g, b):
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return ((v & 0xFF) << 8) | (v >> 8)


def hex_rows(data, per_row=16):
    return ",\n".join("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + per_row])
                      for i in range(0, len(data), per_row))


def main():
    rom = load_cart()
    os.makedirs(OUT_DIR, exist_ok=True)
    with open(os.path.join(OUT_DIR, "hero_rom.h"), "w") as f:
        print("// Generated by romconv/internal/hero/hero_rom_convert.py; do not edit.", file=f)
        print("// H.E.R.O. data blocks at their 2600 addresses, access via hero_data.h.", file=f)
        print("#ifndef HERO_ROM_H\n#define HERO_ROM_H\n\n#include <stdint.h>\n", file=f)
        for name, bank, first, last in BLOCKS:
            data = bytearray(rom[bank * 4096 + (first & 0xFFF): bank * 4096 + (last & 0xFFF) + 1])
            for lo, hi in CODE:
                for a in range(max(lo, first), min(hi, last) + 1):
                    data[a - first] = 0
            print(f"// ${first:04X}-${last:04X}", file=f)
            print(f"static const uint16_t {name.upper()}_FIRST = 0x{first:04x};", file=f)
            print(f"static const uint8_t {name}[{len(data)}] = {{\n{hex_rows(data)}\n}};\n", file=f)
        print("#endif", file=f)
    with open(os.path.join(OUT_DIR, "hero_cmap.h"), "w") as f:
        print("// Generated by romconv/internal/hero/hero_rom_convert.py; do not edit.", file=f)
        print("// Index = NTSC colour register >> 1, RGB565 byte-swapped for the SPI display.", file=f)
        print("// Default: colours of the PAL cart, Stella PAL palette.", file=f)
        print("// HERO_NTSC_COLORS: NTSC cart, TIA NTSC palette (MAME tia.cpp).", file=f)
        print("#ifndef HERO_CMAP_H\n#define HERO_CMAP_H\n\n#include <stdint.h>", file=f)
        print('#include "../../config.h"\n\n#ifdef HERO_NTSC_COLORS', file=f)
        print(palette("hero_palette", ntsc_rgb), file=f)
        print("#else", file=f)
        print(palette("hero_palette", pal_rgb), file=f)
        print("#endif\n\n#endif", file=f)
    print("hero: wrote hero_rom.h, hero_cmap.h")


main()
