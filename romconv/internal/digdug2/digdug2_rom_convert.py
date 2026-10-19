#!/usr/bin/env python3
# ============================================================
# Dig Dug II (Namco 1985) and Motos (Namco 1985) ROM converter
# Usage: digdug2_rom_convert.py [digdug2|motos]
#
# MAME mappy.cpp: Motos has the same ROM regions, sizes and gfx
# layouts as Dig Dug II, only other files.
#
# Same board as todruaga (MAME mappy.cpp: todruaga = digdug2 +
# gfx_todruaga), see todruaga_rom_convert.py. Differences:
#  - sprites 2x 16KB (d21_6.3m + d21_7.3n) = 256 sprites (todruaga 128)
#  - sprite lookup d21-7.5k 0x100 bytes = 16 color groups (todruaga 64)
# ============================================================

import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join("..", "pyconv"))
from gfxutil import load_file, mame_decode, rot_galagino
from asset_emit import emit_compressed
from namco_hw import rgb565_swapped_packed as rgb565_swapped

# (name, size, sha1) from MAME ROM_START(digdug2) "New Ver."
DIGDUG2 = [
    ("d23_3.1d", 0x4000, "d6796479ebb00081e9ae281380a4ce75f730766e"),
    ("d23_1.1b", 0x4000, "698a5c425e23627331d85216a4edee9c391e5749"),
    ("d21_4.1k", 0x2000, "0e46204089cc6e5ffab0d2a62f9a1728f8c35948"),
    ("d21_5.3b", 0x1000, "c9a54df22b0b92efbe7417a00200587225906b46"),
    ("d21_6.3m", 0x4000, "004fba630018dbf03c4b0e284c98077e19fface3"),
    ("d21_7.3n", 0x4000, "77d8d8e6039272f73e63c8f76084138ec613365a"),
    ("d21-5.5b", 0x0020, "77e840d10ab59708a051c3b15305b33d431ee06d"),
    ("d21-6.4c", 0x0100, "bd6bd641c9f220b6a2cc414a1117d5c089571400"),
    ("d21-7.5k", 0x0100, "30a4593726f5a4791e0812fd593e592087e730e3"),
    ("d21-3.3m", 0x0100, "f4f02977130110be52f4dd82fc3c0d02f45778b9"),
]

# (name, size, sha1) from MAME ROM_START(motos), same order as DIGDUG2
MOTOS = [
    ("mo1_3.1d", 0x4000, "ade809a73ac24494b9f95f65b7592df5f86dce60"),
    ("mo1_1.1b", 0x4000, "b050495fcc4a4d93551b29d4f05e49f64017c870"),
    ("mo1_4.1k", 0x2000, "a8b195acfec542734751de29c9dafc2b165a5881"),
    ("mo1_5.3b", 0x1000, "4af9bf2ae9bb78d2d029ef38809181ffa3c0eb66"),
    ("mo1_6.3m", 0x4000, "664679f9d3d74a3fccb086af910392b4fe40c9bc"),
    ("mo1_7.3n", 0x4000, "2b49cdec516e23783f2a291633d81ab8bd0245fc"),
    ("mo1-5.5b", 0x0020, "66b0619affcc5168b099108800a941d6e2416ab0"),
    ("mo1-6.4c", 0x0100, "24eb167266752b064689662e3ef0f62d0407ac26"),
    ("mo1-7.5k", 0x0100, "543adb5348db81ea82a5c039451001ebd82735e3"),
    ("mo1-3.3m", 0x0100, "e21a0618c0f8e35ce26666b6850ac9c0d95d7971"),
]

SETS = {"digdug2": DIGDUG2, "motos": MOTOS}
GAME = sys.argv[1] if len(sys.argv) > 1 else "digdug2"
REQUIRED = SETS[GAME]
ROM_SET = os.path.normpath(os.path.join("..", "..", "..", "romszip", f"{GAME}.zip"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "..", "source", "src", "machines", GAME))
MAIN_HI, MAIN_LO, SUB, TILES, SPR_EVEN, SPR_ODD, PAL, CHAR_LUT, SPR_LUT, WAVE = [r[0] for r in REQUIRED]

NUM_SPRITES = 256
SPRITE_GROUPS = 16

# charlayout pacman/mappy: 8x8x2bpp, 16 bytes/char
CHAR_PLANES = [0, 4]
CHAR_XOFFS = [64, 65, 66, 67, 0, 1, 2, 3]
CHAR_YOFFS = [y * 8 for y in range(8)]

# spritelayout_4bpp: 16x16x4bpp, 128 bytes/sprite (16 bit interleaved)
SPR_PLANES = [0, 4, 8, 12]
SPR_XOFFS = [0,1,2,3, 128,129,130,131, 256,257,258,259, 384,385,386,387]
SPR_YOFFS = [y*16 for y in range(8)] + [512 + y*16 for y in range(8)]


def write_tiles(tiles):
    with open(os.path.join(OUT_DIR, f"{GAME}_tilemap.h"), "w") as f:
        print("// Dig Dug II tiles (d21_5.3b, ROMREGION_INVERT): 256 tiles 8x8 2bpp", file=f)
        print("// pixels LSB-first (blit: (pix>>2c)&3)", file=f)
        flat = []
        for t in tiles:
            for y in range(8):
                v = 0
                for x in range(8):
                    v |= t[y][x] << (2*x)
                flat.append(v)
        emit_compressed(f, f"{GAME}_tilemap", "unsigned short", "[8]", len(tiles), flat)


def write_sprites(sprites):
    # unflipped: digdug2::blit_sprite flips at runtime
    with open(os.path.join(OUT_DIR, f"{GAME}_spritemap.h"), "w") as f:
        print("// Dig Dug II sprites (d21_6.3m+d21_7.3n interleaved): 256 sprites 16x16 4bpp", file=f)
        print("// [code][row*2+half]: nibbles LSB-first, [2r]=pixels 0-7, [2r+1]=pixels 8-15", file=f)
        flat = []
        for t in sprites:
            for y in range(16):
                v = 0
                for x in range(16):
                    v |= t[y][x] << (4*x)
                flat.append(v & 0xffffffff)
                flat.append(v >> 32)
        emit_compressed(f, f"{GAME}_sprites", "uint32_t", "[32]", len(sprites), flat)


def write_colormaps(pal_prom, char_lut, spr_lut):
    pal = [rgb565_swapped(c) for c in pal_prom]  # 32 colors
    # 0x0000 is transparent in the blitters: real black becomes near-black
    def nudge(v):
        return v if v != 0 else 0x2000
    with open(os.path.join(OUT_DIR, f"{GAME}_cmap.h"), "w") as f:
        print("// Dig Dug II colormaps from d21-6.4c (chars, pen+0x10) and d21-7.5k", file=f)
        print("// (sprites, 16 groups x 16). Hardware transparency = lookup 0xF:", file=f)
        print("//  - tiles: opaque real color", file=f)
        print("//  - tiles_prio: 0x0000 where lookup==0xF (priority tiles over sprites)", file=f)
        print("//  - sprites: 0x0000 where lookup==0xF", file=f)
        print(f"const unsigned short {GAME}_colormap_tiles[][4] = {{", file=f)
        rows = []
        for g in range(64):
            vals = [hex(nudge(pal[16 + (char_lut[g*4+p] & 0x0f)])) for p in range(4)]
            rows.append("{" + ",".join(vals) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)
        print(f"const unsigned short {GAME}_colormap_tiles_prio[][4] = {{", file=f)
        rows = []
        for g in range(64):
            vals = []
            for p in range(4):
                lut = char_lut[g*4+p] & 0x0f
                vals.append(hex(0) if lut == 0x0f else hex(nudge(pal[16 + lut])))
            rows.append("{" + ",".join(vals) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)
        flat = []
        for g in range(SPRITE_GROUPS):
            for p in range(16):
                lut = spr_lut[g*16+p] & 0x0f
                flat.append(0 if lut == 0x0f else nudge(pal[lut]))
        emit_compressed(f, f"{GAME}_colormap_sprites", "unsigned short", "[16]",
                               SPRITE_GROUPS, flat)


def write_rom(name, sym, data, comment):
    with open(os.path.join(OUT_DIR, name), "w") as f:
        print(f"// {comment}", file=f)
        emit_compressed(f, sym, "unsigned char", "", len(data), list(data))


def write_wavetable(prom):
    # 256 bytes, low nibble = 8 waveforms x 32 samples, centered (-8..7)
    with open(os.path.join(OUT_DIR, f"{GAME}_wavetable.h"), "w") as f:
        print("// Dig Dug II WSG 15XX waveforms (d21-3.3m): 8 waves x 32 samples", file=f)
        print(f"const signed char {GAME}_wavetable[][32] = {{", file=f)
        rows = []
        for w in range(8):
            vals = [str((prom[w*32+i] & 0x0f) - 8) for i in range(32)]
            rows.append(" { " + ",".join(vals) + " }")
        print(",\n".join(rows), file=f)
        print("};", file=f)


def main():
    print(f"Load ROM from: {os.path.abspath(ROM_SET)}")
    print(f"Target files:  {os.path.abspath(OUT_DIR)}")

    if not os.path.isfile(ROM_SET):
        print(f"ERROR: missing {ROM_SET}", file=sys.stderr)
        sys.exit(1)

    os.makedirs(OUT_DIR, exist_ok=True)

    roms = {}
    for name, size, sha1 in REQUIRED:
        roms[name] = load_file(ROM_SET, [name], sha1)
        assert len(roms[name]) == size, name

    # tiles (ROMREGION_INVERT)
    tdata = bytes(b ^ 0xFF for b in roms[TILES])
    tiles = [rot_galagino(t) for t in
             mame_decode(tdata, 8, 8, CHAR_PLANES, CHAR_XOFFS, CHAR_YOFFS, 128, 256)]

    # sprites: 16 bit interleaved stream (d21_6 = even bytes, d21_7 = odd)
    r6, r7 = roms[SPR_EVEN], roms[SPR_ODD]
    sdata = bytearray(0x8000)
    sdata[0::2] = r6
    sdata[1::2] = r7
    sprites = [rot_galagino(s) for s in
               mame_decode(bytes(sdata), 16, 16, SPR_PLANES, SPR_XOFFS, SPR_YOFFS, 1024, NUM_SPRITES)]
    assert len(sprites) == NUM_SPRITES

    main_rom = roms[MAIN_HI] + roms[MAIN_LO]
    write_tiles(tiles)
    write_sprites(sprites)
    write_colormaps(roms[PAL], roms[CHAR_LUT], roms[SPR_LUT])
    write_rom(f"{GAME}_rom_main.h", f"{GAME}_rom_main", main_rom,
              f"{GAME} main M6809 ROM 0x8000-0xFFFF ({MAIN_HI}+{MAIN_LO})")
    write_rom(f"{GAME}_rom_sub.h", f"{GAME}_rom_sub", roms[SUB],
              f"{GAME} sound M6809 ROM 0xE000-0xFFFF ({SUB})")
    write_wavetable(roms[WAVE])

    # resident (unpacked into the Arena) asset sizes
    sizes = {
        f"{GAME}_tilemap": 256 * 8 * 2,
        f"{GAME}_sprites": NUM_SPRITES * 32 * 4,
        f"{GAME}_colormap_sprites": SPRITE_GROUPS * 16 * 2,
        f"{GAME}_rom_main": len(main_rom),
        f"{GAME}_rom_sub": len(roms[SUB]),
    }
    for name, size in sizes.items():
        print(f"  {name}: {size} bytes unpacked")
    total = sum(sizes.values())
    print(f"Arena budget: {total} bytes (limit 182000)")
    assert total < 182000

    print(f"{GAME} conversion finished.")


if __name__ == "__main__":
    main()
