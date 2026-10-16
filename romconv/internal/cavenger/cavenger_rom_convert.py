#!/usr/bin/env python3
# Cosmic Avenger (Universal 1981), MAME universal/ladybug.cpp, set cavenger.
# Display ROT0 240x192, no rotation: every gfx row is one native line.
# Chars: 8x8 2bpp, 512 codes, uint16_t[8]: word[y] bits 2x = pixel x.
# Sprites 16x16 2bpp, 128 codes, uint32_t[16]; sprites 8x8, 512 codes,
#   uint16_t[8] (same gfx region, MAME spritelayout / spritelayout2).
# Palette: 32 RGB565 (inverted PROM, 470/220 resnet) + sprite lookup.
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join("..", "pyconv"))
from gfxutil import load_file, mame_decode, rgb888_to_rgb565_le
from asset_emit import emit_compressed
from resnet import resistor_weights, combine

ROM_SET = os.path.normpath(os.path.join("..", "..", "..", "romszip", "cavenger.zip"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "..", "source", "src", "machines", "cavenger"))

if not os.path.isfile(ROM_SET):
    print(f"ERROR: missing {ROM_SET}", file=sys.stderr)
    sys.exit(1)
os.makedirs(OUT_DIR, exist_ok=True)


def rom(name, sha1):
    return load_file(ROM_SET, [name], sha1)


main = b"".join(rom(n, s) for n, s in [
    ("1.c4", "f23bd6b9f427c26ac996a5c8ba29f356cf45c78a"),
    ("2.d4", "170e3f8be592dcccb8868474f40f8f2223e8a8b5"),
    ("3.e4", "4b0f1b38a5837b7ffc9aec6c28c6eb72cfa46226"),
    ("4.h4", "f8a7ea799d8ff9b4f830d064bb2f34a76729c336"),
    ("5.j4", "622c90a6c3f0adc24fe8a1d4969075cbd55add4e"),
    ("6.k4", "ec4981fe34abf992acbd6325b2c756c58ff80b04")])
chars = rom("9.f7", "20eaa866b7700535312fd415edaea94408ff3e3d") + rom("0.h7", "bc8c52c6ba919287773ff6a4ec793ebd95176130")
sprites = rom("8.l7", "85f78d5a1e5782587bb66ad101a94fd0d62fb790") + bytes(0x1000)   # 1000-1fff empty
prom_pal = rom("10-2.k1", "03175ee7f8e11896a89d7cc0d614a78a49923627")
prom_spr = rom("10-1.f4", "4c9c76826f3a2a631d01fd2531d55318172b0c12")

CHAR_COUNT, BIG_COUNT, SMALL_COUNT = 512, 128, 512

chars_dec = mame_decode(chars, 8, 8, [0, 512 * 8 * 8], [7, 6, 5, 4, 3, 2, 1, 0],
                        [8 * i for i in range(8)], 8 * 8, CHAR_COUNT)
big_dec = mame_decode(sprites, 16, 16, [1, 0],
                      [0, 2, 4, 6, 8, 10, 12, 14] + [128 + 2 * i for i in range(8)],
                      [16 * i for i in (23, 22, 21, 20, 19, 18, 17, 16, 7, 6, 5, 4, 3, 2, 1, 0)],
                      64 * 8, BIG_COUNT)
small_dec = mame_decode(sprites, 8, 8, [1, 0], [0, 2, 4, 6, 8, 10, 12, 14],
                        [16 * i for i in (7, 6, 5, 4, 3, 2, 1, 0)], 16 * 8, SMALL_COUNT)


def rows2bpp(gfx):
    return [sum(p << (2 * x) for x, p in enumerate(row)) for t in gfx for row in t]


(W,), _ = resistor_weights(0, 255, -1.0, [([470, 220], 470, 0)])


def bit(v, n):
    return (~v >> n) & 1


rgb565 = [rgb888_to_rgb565_le(combine(W, bit(c, 0), bit(c, 5)),
                              combine(W, bit(c, 2), bit(c, 6)),
                              combine(W, bit(c, 4), bit(c, 7))) for c in prom_pal]


def rev4(v):
    return ((v & 1) << 3) | ((v & 2) << 1) | ((v & 4) >> 1) | ((v & 8) >> 3)


# MAME pens 0x20 + i: colors 0-7 from the low nibble, 8-15 from the high nibble
spr_lut = [rev4(b & 0x0F) for b in prom_spr] + [rev4(b >> 4) for b in prom_spr]


def write(name, fn):
    path = os.path.join(OUT_DIR, name)
    with open(path, "w") as f:
        fn(f)
    print(f"wrote {path}")


write("cavenger_rom.h", lambda f: emit_compressed(f, "cavenger_rom", "unsigned char", "", len(main), main))
write("cavenger_chars.h", lambda f: emit_compressed(f, "cavenger_chars", "unsigned short", "[8]", CHAR_COUNT, rows2bpp(chars_dec)))
write("cavenger_sprites.h", lambda f: (
    emit_compressed(f, "cavenger_sprites", "uint32_t", "[16]", BIG_COUNT, rows2bpp(big_dec)),
    emit_compressed(f, "cavenger_small", "unsigned short", "[8]", SMALL_COUNT, rows2bpp(small_dec))))


def palette(f):
    emit_compressed(f, "cavenger_rgb", "unsigned short", "", 32, rgb565)
    emit_compressed(f, "cavenger_lut", "unsigned char", "", 64, spr_lut)


write("cavenger_palette.h", palette)

sizes = {"main": len(main), "chars": CHAR_COUNT * 16, "sprites": BIG_COUNT * 64,
         "small": SMALL_COUNT * 16, "rgb": 64, "lut": 64}
for k, v in sizes.items():
    print(f"  {k:8s} {v:6d}")
total = sum(sizes.values())
print(f"Arena total {total} B (budget 182000)")
assert total < 182000
