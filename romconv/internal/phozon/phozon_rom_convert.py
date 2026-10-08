#!/usr/bin/env python3
# ============================================================
# Phozon (Namco 1983) ROM converter
#
# MAME: namco/mappy.cpp (ROM_START phozon, gfx_phozon),
#       namco/mappy_v.cpp (phozon_palette)
# ============================================================

import os
import sys

sys.dont_write_bytecode = True

sys.path.insert(0, os.path.join("..", "pyconv"))
from gfxutil import load_file, mame_decode, rot_galagino
from asset_emit import emit_compressed
from namco_hw import nudge, rgb565_swapped_rgb

ROM_SET = os.path.normpath(os.path.join("..", "..", "..", "romszip", "phozon.zip"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "..", "source", "src", "machines", "phozon"))

ARENA_CAPACITY = 182000

FILES = {
  "main":   [("6e.rom", "87a948b289356675d0418c87c3c0ae36ceba3ee0"),
             ("6h.rom", "b1d5146c009469d4c6695f08ea2c6ad5d05b5b9b"),
             ("6c.rom", "789881e94743efae01c63c1e3ce8d039cfa0324c"),
             ("6d.rom", "48585ac1eff8fb7ed35f56c767d725cae88ff128")],
  "sound":  [("3b.rom", "2774681ea668403de31ea218d5df3ce64e3b9243")],
  "sub":    [("9r.rom", "2caef680229180b237f8c4becf052f1a96592efd")],
  "chars":  [("7j.rom", "12ef817136b45927d7f279952fa19049a1349f60"),
             ("8j.rom", "e3303656b4e8b988e55a9551e5344e289958f677")],
  "sprites": [("5t.rom", "4e9dda0d5ad1c1b8b3be7edb05b3060f5f63a9c7")],
  "red":    [("red.prm", "b24d9b3354d20a7ecc02c428245669c6c86bfd61")],
  "green":  [("green.prm", "b0ca7f8a77b7208cf974a8cc565fc91b7f40f51f")],
  "blue":   [("blue.prm", "e7b1ed698ab0e87872cb3a8f3ec102ca3a753259")],
  "chrlut": [("chr.prm", "7b1899ca3f33f4561b572de1f24d9ea9d7d84b59")],
  "sprlut": [("sprite.prm", "4305d37e613e1d15d37539b152c948648189c2cd")],
  "wave":   [("sound.prm", "072f427453efb1dda8147da61804fff06e1bc4d5")],
}

# charlayout: 8x8x2bpp, 16 byte/char
CHAR_PLANES = [0, 4]
CHAR_XOFFS = [64, 65, 66, 67, 0, 1, 2, 3]
YOFFS_8 = [y * 8 for y in range(8)]

# spritelayout_8x8: 8x8x2bpp, 16 byte/sprite
SPR_PLANES = [0, 4]
SPR_XOFFS = [0, 1, 2, 3, 64, 65, 66, 67]

COUNT = 512
TRANSPARENT_LUT = 0x0F   # lookup nibble 15: char pen 15 / sprite pen 31

# palette PROM bits 0..3 drive 2.2k, 1k, 470, 220 ohm (no pull resistor)
RES = [2200, 1000, 470, 220]


def load(romset, key):
    return b"".join(load_file(romset, [n], sha1) for n, sha1 in FILES[key])


def color_level(nibble):
    w = [1.0 / r for r in RES]
    return round(255 * sum(w[i] for i in range(4) if nibble & (1 << i)) / sum(w))


def palette(red, green, blue):
    return [nudge(rgb565_swapped_rgb(color_level(red[i] & 0x0F),
                                     color_level(green[i] & 0x0F),
                                     color_level(blue[i] & 0x0F))) for i in range(32)]


def flat_2bpp(tile):
    # one unsigned short per galagino row, pixel 0 in bits 0-1
    out = []
    for row in tile:
        v = 0
        for x, p in enumerate(row):
            v |= p << (2 * x)
        out.append(v)
    return out


def flipped(tile, flipx, flipy):
    rows = tile[::-1] if flipy else tile
    return [r[::-1] if flipx else list(r) for r in rows]


def write_rom(fname, sym, data, comment):
    with open(os.path.join(OUT_DIR, fname), "w") as f:
        print(f"// {comment}", file=f)
        emit_compressed(f, sym, "unsigned char", "", len(data), list(data))
    return len(data)


def write_chars(chars):
    flat = []
    for t in chars:
        flat += flat_2bpp(rot_galagino(t))
    with open(os.path.join(OUT_DIR, "phozon_tilemap.h"), "w") as f:
        print("// Phozon chars (7j+8j): 512 x 8x8 2bpp, rotated, pixel LSB-first", file=f)
        emit_compressed(f, "phozon_tilemap", "unsigned short", "[8]", len(chars), flat)
    return 2 * len(flat)


def write_sprites(sprites):
    # 4 variants per code, index code*4 + (flipy<<1 | flipx) in native MAME
    # flip terms; the flip is applied before the rotation
    flat = []
    for t in sprites:
        for v in range(4):
            flat += flat_2bpp(rot_galagino(flipped(t, v & 1, v >> 1)))
    with open(os.path.join(OUT_DIR, "phozon_spritemap.h"), "w") as f:
        print("// Phozon sprites (5t): 512 x 8x8 2bpp x 4 flip variants, rotated", file=f)
        emit_compressed(f, "phozon_sprites", "unsigned short", "[8]", 4 * len(sprites), flat)
    return 2 * len(flat)


def write_colormaps(pal, chrlut, sprlut):
    def table(f, name, entries):
        print(f"const unsigned short {name}[][4] = {{", file=f)
        rows = ["{" + ",".join(hex(v) for v in entries[g * 4:g * 4 + 4]) + "}" for g in range(64)]
        print(",\n".join(rows), file=f)
        print("};", file=f)

    tiles = [pal[c & 0x0F] for c in chrlut]
    prio = [0 if (c & 0x0F) == TRANSPARENT_LUT else pal[c & 0x0F] for c in chrlut]
    sprites = [0 if (c & 0x0F) == TRANSPARENT_LUT else pal[16 + (c & 0x0F)] for c in sprlut]

    with open(os.path.join(OUT_DIR, "phozon_cmap.h"), "w") as f:
        print("// Phozon colormaps: chars -> palette 0-15, sprites -> 16-31.", file=f)
        print("// 0x0000 = transparent (lookup 15): prio tiles and sprites only.", file=f)
        table(f, "phozon_colormap_tiles", tiles)
        table(f, "phozon_colormap_tiles_prio", prio)
        table(f, "phozon_colormap_sprites", sprites)


def write_wavetable(prom):
    with open(os.path.join(OUT_DIR, "phozon_wavetable.h"), "w") as f:
        print("// Phozon WSG 15XX waveforms (sound.prm): 8 x 32 samples, -8..7", file=f)
        print("const signed char phozon_wavetable[][32] = {", file=f)
        rows = [" { " + ",".join(str((prom[w * 32 + i] & 0x0F) - 8) for i in range(32)) + " }"
                for w in range(8)]
        print(",\n".join(rows), file=f)
        print("};", file=f)


def decode_chars(data):
    return mame_decode(data, 8, 8, CHAR_PLANES, CHAR_XOFFS, YOFFS_8, 128, COUNT)


def decode_sprites(data):
    return mame_decode(data, 8, 8, SPR_PLANES, SPR_XOFFS, YOFFS_8, 128, COUNT)


def convert(romset):
    os.makedirs(OUT_DIR, exist_ok=True)
    print(f"Load ROM from: {os.path.abspath(romset)}")
    print(f"Target files:  {os.path.abspath(OUT_DIR)}")

    sizes = {
        "phozon_rom_main": write_rom("phozon_rom_main.h", "phozon_rom_main", load(romset, "main"),
                                     "Phozon main M6809 ROM 0x8000-0xFFFF (6e+6h+6c+6d)"),
        "phozon_rom_sound": write_rom("phozon_rom_sound.h", "phozon_rom_sound", load(romset, "sound"),
                                      "Phozon sound M6809 ROM 0xE000-0xFFFF (3b)"),
        "phozon_rom_sub": write_rom("phozon_rom_sub.h", "phozon_rom_sub", load(romset, "sub"),
                                    "Phozon sub M6809 ROM 0xE000-0xFFFF (9r)"),
        "phozon_tilemap": write_chars(decode_chars(load(romset, "chars"))),
        "phozon_sprites": write_sprites(decode_sprites(load(romset, "sprites"))),
    }

    pal = palette(load(romset, "red"), load(romset, "green"), load(romset, "blue"))
    write_colormaps(pal, load(romset, "chrlut"), load(romset, "sprlut"))
    write_wavetable(load(romset, "wave"))

    for name, size in sizes.items():
        print(f"  {name:18s} {size:6d} B")
    total = sum(sizes.values())
    print(f"  Arena total        {total:6d} / {ARENA_CAPACITY} B")
    if total >= ARENA_CAPACITY:
        print("ERROR: assets exceed Arena capacity")
        sys.exit(1)
    print("Phozon conversion finished.")


def main():
    if not os.path.isfile(ROM_SET):
        print("ERROR: No roms.")
        sys.exit(1)
    convert(ROM_SET)


if __name__ == "__main__":
    main()
