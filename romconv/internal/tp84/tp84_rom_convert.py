#!/usr/bin/env python3
# Time Pilot '84 (Konami 1984), MAME konami/tp84.cpp, set tp84.
# Tiles: 8x8 2bpp, stored rotated (row = native x, pixel k = native y 7-k),
#   2 bits per pixel LSB first in a uint16_t, for tp84's per-line bg render.
# Sprites: 16x16 4bpp native landscape [16][8], low nibble = even x; the
#   blit rotates and flips at runtime (circusc).
# Palette: 256 RGB565 from the 3 PROMs (MAME resnet weights) + char and
#   sprite lookups; the per-bank color maps are built at runtime.
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join("..", "pyconv"))
from gfxutil import load_file, mame_decode, rot_galagino, rgb888_to_rgb565_le
from asset_emit import emit_compressed
from resnet import resistor_weights, combine

ROM_SET = os.path.normpath(os.path.join("..", "..", "..", "romszip", "tp84.zip"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "..", "source", "src", "machines", "tp84"))

if not os.path.isfile(ROM_SET):
    print(f"ERROR: missing {ROM_SET}", file=sys.stderr)
    sys.exit(1)
os.makedirs(OUT_DIR, exist_ok=True)


def rom(name, sha1):
    return load_file(ROM_SET, [name], sha1)


main = (rom("388_f04.7j", "6848ef35ec7f92cccefb0fb2de42c4b0e9ec476f")
        + rom("388_05.8j", "f3bb1ee66c9e47d050370ac9ca74f3020cb9cfa3")
        + rom("388_f06.9j", "65dee1fd4c940a5423d57cb55a7f2ad89c59c5c6")
        + rom("388_07.10j", "896e31c59aedf1c7e73e6f30fbe78cc020b457ab"))
sub = rom("388_f08.10d", "118a1b46ee01a583e6cf39af59b073321c76dbff")
audio = rom("388j13.6a", "981289f5bdf7dc1348f4ca547ac933ef503b6588")
tiles = (rom("388_h02.2j", "1a3c7cd47ad34e37a7b0f3014e10c055cbb2b559")
         + rom("388_d01.1j", "6975f3a1603b14132aab58329195a4845a6e28bb"))
sprites = (rom("388_e09.12a", "6f48d3efc53d63171ec655e64b225412de1374e4")
           + rom("388_e10.13a", "7e2dde080bb614709561431a81b0490b2aaa42a9")
           + rom("388_e11.14a", "792aaa4daedc8eb807d5a66d87da4641739b1660")
           + rom("388_e12.15a", "d90f99b19ab3cddfdfd37a273fb437be098088bc"))
prom_r = rom("388d14.2c", "e39026f87f5b995cf4a38b5d3d3fee7561762ae6")
prom_g = rom("388d15.2d", "f09d8b92c7f9bf046cdd815c5282d0510e61b6e0")
prom_b = rom("388d16.1e", "9ba8af294dbd6f3a5d039c74a56e0605a913c037")
char_lut = rom("388d18.1f", "3f74ad733b07b6a31cf9d4956d171eb9253dd6bf")
spr_lut = rom("388j17.16c", "42ab23206be99e840bd9c52cefa175c12fac8e5b")

# --- tiles: MAME charlayout, rotated for galagino ---
tiles_dec = mame_decode(tiles, 8, 8, [4, 0], [0, 1, 2, 3, 64, 65, 66, 67],
                        [8 * i for i in range(8)], 128, 1024)
tile_words = []
for t in tiles_dec:
    r = rot_galagino(t)          # r[nx][k] = t[7-k][nx]: row = native x, pixel k = native y 7-k
    for row in r:
        w = 0
        for k, p in enumerate(row):
            w |= p << (2 * k)
        tile_words.append(w)

# --- sprites: MAME spritelayout, native orientation ---
half = len(sprites) * 8 // 2
spr_dec = mame_decode(sprites, 16, 16, [half + 4, half + 0, 4, 0],
                      [0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195],
                      [8 * i for i in range(8)] + [256 + 8 * i for i in range(8)], 512, 256)
spr_bytes = []
for s in spr_dec:
    for row in s:
        for x in range(0, 16, 2):
            spr_bytes.append(row[x] | (row[x + 1] << 4))

# --- palette: MAME compute_resistor_weights(0, 255, -1, 4, {1000,470,220,100}, w, 470, 0) ---
(W,), _ = resistor_weights(0, 255, -1.0, [([1000, 470, 220, 100], 470, 0)])


def comp(v):
    return combine(W, v & 1, (v >> 1) & 1, (v >> 2) & 1, (v >> 3) & 1)


rgb888 = [(comp(prom_r[i]), comp(prom_g[i]), comp(prom_b[i])) for i in range(256)]
# title screen colors from a MAME snapshot (frame 440): must be in the palette
for c in [(224, 224, 224), (175, 175, 45)]:
    if c not in rgb888:
        near = sorted(set(rgb888), key=lambda p: sum((a - b) ** 2 for a, b in zip(p, c)))[:6]
        print(f"nearest to {c}: {near}", file=sys.stderr)
    assert c in rgb888, f"palette mismatch: {c} not in computed palette"
rgb565 = [rgb888_to_rgb565_le(*c) for c in rgb888]
lut = [b & 0x0F for b in char_lut] + [b & 0x0F for b in spr_lut]


def write(name, fn):
    path = os.path.join(OUT_DIR, name)
    with open(path, "w") as f:
        fn(f)
    print(f"wrote {path}")


# uint16_t == unsigned short; emit_compressed knows the latter
write("tp84_rom_main.h", lambda f: emit_compressed(f, "tp84_rom_main", "unsigned char", "", len(main), main))
write("tp84_rom_sub.h", lambda f: emit_compressed(f, "tp84_rom_sub", "unsigned char", "", len(sub), sub))
write("tp84_rom_audio.h", lambda f: emit_compressed(f, "tp84_rom_audio", "unsigned char", "", len(audio), audio))
write("tp84_tiles.h", lambda f: emit_compressed(f, "tp84_tiles", "unsigned short", "[8]", 1024, tile_words))
write("tp84_sprites.h", lambda f: emit_compressed(f, "tp84_sprites", "unsigned char", "[16][8]", 256, spr_bytes))


def palette(f):
    emit_compressed(f, "tp84_rgb", "unsigned short", "", 256, rgb565)
    emit_compressed(f, "tp84_lut", "unsigned char", "", 512, lut)


write("tp84_palette.h", palette)

# --- eyeball previews (optional, $TMPDIR/tp84) ---
tmp = os.environ.get("TMPDIR")
if tmp:
    from gfxutil import render_tile_grid
    d = os.path.join(tmp, "tp84")
    os.makedirs(d, exist_ok=True)
    grey = {i: (i * 85, i * 85, i * 85) for i in range(4)}
    render_tile_grid(tiles_dec, 8, 8, grey, 32, os.path.join(d, "tiles.png"))
    grey16 = {i: (i * 17, i * 17, i * 17) for i in range(16)}
    render_tile_grid(spr_dec, 16, 16, grey16, 16, os.path.join(d, "sprites.png"))

sizes = {"main": len(main), "sub": len(sub), "audio": len(audio),
         "tiles": 1024 * 16, "sprites": 256 * 128, "rgb": 512, "lut": 512}
for k, v in sizes.items():
    print(f"  {k:8s} {v:6d}")
total = sum(sizes.values())
print(f"Arena total {total} B (CAPACITY 201500)")
assert total < 201500
