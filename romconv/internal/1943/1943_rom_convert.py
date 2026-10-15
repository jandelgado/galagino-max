#!/usr/bin/env python3
"""Convert MAME set 1943b (romszip/1943.zip, merged with parent 1943) into
galagino headers (MAME capcom/1943.cpp, 1943_v.cpp).

Gfx are stored for galagino's ROT270 strips: one "line" per native x
column, its pixels along native y (panel left to right), packed per
pixel at 2 bits (chars) or 4 bits (tiles, sprites):
  chars   unsigned short[8]  line x: pixel y at bits 2y
  sprites uint32_t[32]       line x: words 2x, 2x+1, pixel y at nibble y
  tiles   uint32_t[128]      line x: words 4x..4x+3 (32x32 tiles)
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "pyconv"))
from gfxutil import load_file, mame_decode, rgb888_to_rgb565_le
from asset_emit import emit_compressed, emit_plain, _hex_block
from convutil import fatal

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..", "..")
ZIP = os.path.join(ROOT, "romszip", "1943.zip")
OUT = os.path.join(ROOT, "source", "src", "machines", "1943")

ARENA_BLOCKS = (91000, 110500)  # source/src/emulation/arena.h BLOCK_A_SIZE, BLOCK_B_SIZE
ARENA_BUDGET = 182000

# MAME ROM_START( 1943b ); bootleg files live in 1943b/ of the merged zip
MAIN = [("1943b/1.12d", "6f84e906656f132ffcb63022f6d067580d261431"),
        ("1943b/bm02.13d", "af1d8ce73e8671b7b41248ce6486c9b5aaf6a233"),
        ("1943b/bm03.14d", "f66d2356b413a418c887b4085a5315475c7a8bba")]
SOUND = [("bm05.4k", "4d2d019a9f8452fbbb247e893280568a2e86073e")]
CHARS = [("1943b/4.5h", "4833ad9f747b529ce92c4993388ab3516f8df4ed")]
BG1 = [("1943b/15.12f", "181d7b0a1ca0cfcc3bd71f0b97dc80f7ff27a9c6"),
       ("1943b/16.14f", "a3930dea9b64e84ade78dae4a631ebb7e9741954"),
       ("1943b/17.12j", "b9852c476110db3f654152ca85265d184a1a816e"),
       ("1943b/18.14j", "14ae016279628732b397db9a526bfda7ede0be5a")]
BG2 = [("bm24.14k", "88da112ab9fc7e0d8f0e901f273715b950ae588c"),
       ("bm25.14l", "19fe3c714b1d52cbb21dea25cdee5af841f525db")]
SPRITES = [("bm06.10a", "c9fa07cb61f6905408b355edabfe453fb652ff0d"),
           ("bm07.11a", "6367c7e80e80d4a0d33d7840b5c843c63c80123e"),
           ("bm08.12a", "755c27a07728fd686168e9d9e4dee3d8f274892a"),
           ("bm09.14a", "3ab299bad1ba115efead53ebd92254abe7a092ba"),
           ("bm10.10c", "873629b00cf3f6d8976a7fdafe63cd16e47b7491"),
           ("bm11.11c", "c213d5c3e76a5749bc32539604716dcef6dcb694"),
           ("bm12.12c", "fef271a38dc1a9e45a0c6e27e28e713c77c8f8c9"),
           ("bm13.14c", "2b3a65e354a205c05a87f783e9938b64bc62396f")]
TILEMAP = [("bm14.5f", "ce4f6dbf8fa030ad45cbb5afd58df27fed2d4618"),
           ("bm23.8k", "45b0283d84d394c16c35802463ca95d70d1062d4")]
PROMS = {
    "red": ("bm1.12a", "5b8b59f6f4e5ad358611de50608f47f41a5b0e51"),
    "green": ("bm2.13a", "1796c4c9041dfe28e6319576f21df1dbcb8d12bf"),
    "blue": ("bm3.14a", "d1118159b3d429d841e4efa938728ebedadd7ec5"),
    "char_lut": ("bm5.7f", "fa609f6d675af18c379838583505724d28bcff0e"),
    "bg1_lut": ("bm10.7l", "13da924e4b182759c4aae49034f3a7cbe556ea65"),
    "bg1_bank": ("bm9.6l", "98f4570ee061e9aa58d8ed2d2f8ae59ce2ec5795"),
    "bg2_lut": ("bm12.12m", "684f04d9a5b94ae1db5fb95763e65271f4cf8e01"),
    "bg2_bank": ("bm11.12l", "94a06f81b775c4e49d57d42fc064d3072a253bbd"),
    "spr_lut": ("bm8.8c", "be9852500209066e2f0ff2770e0c217d1636a0b5"),
    "spr_bank": ("bm7.7c", "9f5e6db464d21457a33ec8bdfdff069632b791db"),
}
BG1_CLEAR = 0x0f   # configure_groups(gfx2, 0x0f): lookup 0x0f is transparent
SPR_BEHIND = 0x08  # bm7.7c bit 3: sprite pen hidden behind bg1


def load(files):
    return b"".join(bytes(load_file(ZIP, [name], sha1)) for name, sha1 in files)


def step(start, n, inc=1):
    return [start + i * inc for i in range(n)]


def decode_chars(data):
    # charlayout: planes {4, 0}, x STEP4(0,1) STEP4(8,1), y STEP8(0,16)
    return mame_decode(data, 8, 8, [4, 0], step(0, 4) + step(8, 4), step(0, 8, 16), 128, len(data) // 16)


def decode_tiles(data):
    # tilelayout: 32x32, planes {half+4, half+0, 4, 0}, 256 bytes per tile
    half = len(data) * 4
    xoffs = []
    for blk in range(4):
        xoffs += step(blk * 512, 4) + step(blk * 512 + 8, 4)
    return mame_decode(data, 32, 32, [half + 4, half, 4, 0], xoffs, step(0, 32, 16), 2048, len(data) // 512)


def decode_sprites(data):
    # spritelayout: 16x16, planes {half+4, half+0, 4, 0}, 64 bytes per sprite
    half = len(data) * 4
    xoffs = step(0, 4) + step(8, 4) + step(256, 4) + step(264, 4)
    return mame_decode(data, 16, 16, [half + 4, half, 4, 0], xoffs, step(0, 16, 16), 512, len(data) // 128)


def lines(tile, bpp):
    """Pack a decoded tile (tile[y][x]) as galagino lines (see module doc)."""
    size = len(tile)
    per_word = min(32 // bpp, size)
    words = []
    for x in range(size):
        for w in range(0, size, per_word):
            v = 0
            for i in range(per_word):
                v |= tile[w + i][x] << (bpp * i)
            words.append(v)
    return words


def unpack(words, size, bpp):
    per_word = min(32 // bpp, size)
    wpl = size // per_word
    tile = [[0] * size for _ in range(size)]
    for x in range(size):
        for y in range(size):
            tile[y][x] = (words[x * wpl + y // per_word] >> (bpp * (y % per_word))) & ((1 << bpp) - 1)
    return tile


def body32(rows):
    return ",\n".join("  {" + ",".join("0x%08x" % v for v in r) + "}" for r in rows)


def emit_plain_bytes(name, data):
    path = os.path.join(OUT, name.lstrip("_") + ".h")
    with open(path, "w", newline="\n") as f:
        f.write("// Generated by romconv/internal/1943/1943_rom_convert.py; do not edit.\n")
        emit_plain(f, name, "unsigned char", "", len(data), "  " + _hex_block(data))
    return path


def palette():
    r, g, b = (load_file(ZIP, [PROMS[c][0]], PROMS[c][1]) for c in ("red", "green", "blue"))

    # 4 bit per gun: 2.2k, 1k, 470, 220 ohm
    def level(v):
        return 0x0e * (v & 1) + 0x1f * ((v >> 1) & 1) + 0x43 * ((v >> 2) & 1) + 0x8f * ((v >> 3) & 1)

    return [rgb888_to_rgb565_le(level(r[i]), level(g[i]), level(b[i])) for i in range(256)]


def main():
    sizes = {}  # Arena residents: name -> (bytes, block)

    rom = load(MAIN)
    if len(rom) != 0x28000:
        fatal("1943b main ROM size")
    # PLAIN: banked CPU ROM larger than the Arena budget
    emit_plain_bytes("_1943_rom", rom)
    # PLAIN: no room left in either Arena block (see budget below)
    emit_plain_bytes("_1943_sound", load(SOUND))

    # chars: 2048 x 8x8 2bpp
    chars = decode_chars(load(CHARS))
    char_lines = [lines(t, 2) for t in chars]
    with open(os.path.join(OUT, "1943_chars.h"), "w", newline="\n") as f:
        f.write("// Generated by romconv/internal/1943/1943_rom_convert.py; do not edit.\n")
        flat = [w for words in char_lines for w in words]
        emit_compressed(f, "_1943_chars", "unsigned short", "[8]", len(chars), flat)
    sizes["chars"] = (len(chars) * 16, 1)

    # bg1 (front, clouds): 512 x 32x32 4bpp
    bg1 = [lines(t, 4) for t in decode_tiles(load(BG1))]
    with open(os.path.join(OUT, "1943_bg1.h"), "w", newline="\n") as f:
        f.write("// Generated by romconv/internal/1943/1943_rom_convert.py; do not edit.\n")
        # PLAIN: 256 KB, larger than the Arena; read per visible line
        emit_plain(f, "_1943_bg1", "uint32_t", "[128]", len(bg1), body32(bg1))

    # bg2 (back, sea): 128 x 32x32 4bpp
    bg2_tiles = decode_tiles(load(BG2))
    bg2 = [lines(t, 4) for t in bg2_tiles]
    with open(os.path.join(OUT, "1943_bg2.h"), "w", newline="\n") as f:
        f.write("// Generated by romconv/internal/1943/1943_rom_convert.py; do not edit.\n")
        emit_compressed(f, "_1943_bg2", "uint32_t", "[128]", len(bg2), [v for t in bg2 for v in t])
    sizes["bg2"] = (len(bg2) * 512, 1)

    # sprites: 2048 x 16x16 4bpp
    sprites = [lines(t, 4) for t in decode_sprites(load(SPRITES))]
    with open(os.path.join(OUT, "1943_sprites.h"), "w", newline="\n") as f:
        f.write("// Generated by romconv/internal/1943/1943_rom_convert.py; do not edit.\n")
        # PLAIN: 256 KB, larger than the Arena; read per visible sprite
        emit_plain(f, "_1943_sprites", "uint32_t", "[32]", len(sprites), body32(sprites))

    # tilemaps: bg1 at 0x0000, bg2 at 0x8000 (2 bytes per 32x32 tile)
    tilemap = load(TILEMAP)
    with open(os.path.join(OUT, "1943_tilemap.h"), "w", newline="\n") as f:
        f.write("// Generated by romconv/internal/1943/1943_rom_convert.py; do not edit.\n")
        emit_compressed(f, "_1943_tilemap", "unsigned char", "", len(tilemap), list(tilemap))
    sizes["tilemap"] = (len(tilemap), 0)

    # colors per layer, RGB565 (byte swapped) per color and pen
    pal = palette()
    p = {k: load_file(ZIP, [v[0]], v[1]) for k, v in PROMS.items()}
    char_ct = [(p["char_lut"][i] & 0x0f) | 0x40 for i in range(128)]
    bg1_ct = [((p["bg1_bank"][i] & 3) << 4) | (p["bg1_lut"][i] & 0x0f) for i in range(256)]
    bg2_ct = [((p["bg2_bank"][i] & 3) << 4) | (p["bg2_lut"][i] & 0x0f) for i in range(256)]
    spr_ct = [0x80 | ((p["spr_bank"][i] & 7) << 4) | (p["spr_lut"][i] & 0x0f) for i in range(256)]
    bg1_clear = [sum(1 << pen for pen in range(16) if bg1_ct[c * 16 + pen] == BG1_CLEAR) for c in range(16)]
    spr_behind = [sum(1 << pen for pen in range(16) if p["spr_bank"][c * 16 + pen] & SPR_BEHIND) for c in range(16)]
    with open(os.path.join(OUT, "1943_cmap.h"), "w", newline="\n") as f:
        f.write("// Generated by romconv/internal/1943/1943_rom_convert.py; do not edit.\n")
        emit_compressed(f, "_1943_cmap_chars", "unsigned short", "[4]", 32, [pal[c] for c in char_ct])
        emit_compressed(f, "_1943_cmap_bg1", "unsigned short", "[16]", 16, [pal[c] for c in bg1_ct])
        emit_compressed(f, "_1943_cmap_bg2", "unsigned short", "[16]", 16, [pal[c] for c in bg2_ct])
        emit_compressed(f, "_1943_cmap_sprites", "unsigned short", "[16]", 16, [pal[c] for c in spr_ct])
        # row 0: bg1 pens that are transparent, row 1: sprite pens behind bg1
        emit_compressed(f, "_1943_pen_masks", "unsigned short", "[16]", 2, bg1_clear + spr_behind)
    sizes["cmaps"] = (128 * 2 + 3 * 256 * 2 + 64, 0)

    # self-check: packed lines decode back to MAME's pixels
    for t in (0, 1, 127):
        if unpack(bg2[t], 32, 4) != bg2_tiles[t]:
            fatal(f"bg2 tile {t} line packing")
    if unpack(char_lines[0x41], 8, 2) != chars[0x41]:
        fatal("char line packing")

    blocks = [0, 0]
    for name, (size, block) in sizes.items():
        blocks[block] += size
        print(f"  arena {name:8s} {size:6d} block {'AB'[block]}")
    for i, used in enumerate(blocks):
        if used > ARENA_BLOCKS[i]:
            fatal(f"1943 block {'AB'[i]} {used} > {ARENA_BLOCKS[i]}")
    total = sum(blocks)
    print(f"  arena total    {total:6d} of {ARENA_BUDGET} (A {blocks[0]}, B {blocks[1]})")
    if total >= ARENA_BUDGET:
        fatal("1943 assets exceed the Arena budget")


if __name__ == "__main__":
    main()
