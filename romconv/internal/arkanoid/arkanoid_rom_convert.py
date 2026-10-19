#!/usr/bin/env python3
# Arkanoid (Taito 1986), MAME taito/arkanoid.cpp, set arkangc (Game
# Corporation bootleg, no MCU). ROT90: native 256x224 -> panel 224x256.
# Tiles: 8x8 3bpp, 4096 codes (2 gfx banks of 2048), deduplicated: the
#   map turns a native code into an index of the unique tiles. Rotated for
#   the portrait panel (rot_galagino): word[r] = native column r, pixel x
#   (panel column) in bits 31-28 - 4x. Sprites use the same tiles.
# Palette: 512 RGB565 from 3 PROMs (MAME RGB_444_PROMS weights), pen = color * 8 + pixel.
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join("..", "pyconv"))
from gfxutil import load_file, mame_decode, rot_galagino, rgb888_to_rgb565_le
from arena_layout import LOCAL
from asset_emit import emit_compressed

ROM_SET = os.path.normpath(os.path.join("..", "..", "..", "romszip", "arkanoid.zip"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "..", "source", "src", "machines", "arkanoid"))

if not os.path.isfile(ROM_SET):
    print(f"ERROR: missing {ROM_SET}", file=sys.stderr)
    sys.exit(1)
os.makedirs(OUT_DIR, exist_ok=True)


def rom(name, sha1):
    return load_file(ROM_SET, [name], sha1)


ROM_SIZE = 0xC000   # 0000-bfff mapped; second half of arkgc.2 unused
main = (rom("arkangc/arkgc.1", "beb759cee68009a06824b755d2aa26d7d436b5b0") +
        rom("arkangc/arkgc.2", "731c9224616a338084edd6944c754d68eabba7f2"))[:ROM_SIZE]
gfx = (rom("a75__03.ic64", "ac053cc4908b4075f918748b89570e07a0ba5116") +
       rom("a75__04.ic63", "5d253c46ccf4cd2976a5fb8b8713f0f345443d06") +
       rom("a75__05.ic62", "7520dd48de20db60a2038f134dcaa454988e7874"))
prom = (rom("a75-07.ic24", "6bc589e8a609b4cf450aebedc8ce02d5d45c970f") +
        rom("a75-08.ic23", "c14f56b8ef103600862e7930709d293b0aa97a73") +
        rom("a75-09.ic22", "adaa003dcd981576ea1cc5f697d709b2d6b2ea29"))

TILE_COUNT, COLORS = 4096, 512
# MAME charlayout: planes {2*4096*64, 4096*64, 0}, first plane = MSB
tiles = mame_decode(gfx, 8, 8, [2 * TILE_COUNT * 64, TILE_COUNT * 64, 0], list(range(8)),
                    [8 * i for i in range(8)], 64, TILE_COUNT)


def pack(tile):
    return tuple(sum(p << (28 - 4 * x) for x, p in enumerate(row)) for row in rot_galagino(tile))


unique, index, tile_map = [], {}, []
for t in tiles:
    key = pack(t)
    if key not in index:
        index[key] = len(unique)
        unique.append(key)
    tile_map.append(index[key])



# MAME palette_device::palette_init_rgb_444_proms: resistor weights per bit
def level(v):
    return 0x0e * (v & 1) + 0x1f * ((v >> 1) & 1) + 0x43 * ((v >> 2) & 1) + 0x8f * ((v >> 3) & 1)


rgb565 = [rgb888_to_rgb565_le(level(prom[i]), level(prom[COLORS + i]), level(prom[2 * COLORS + i]))
          for i in range(COLORS)]


def write(name, fn):
    path = os.path.join(OUT_DIR, name)
    with open(path, "w") as f:
        fn(f)
    print(f"wrote {path}")


write("arkanoid_rom.h", lambda f: emit_compressed(f, "arkanoid_rom", "unsigned char", "", len(main), main))
write("arkanoid_tiles.h", lambda f: (
    emit_compressed(f, "arkanoid_tiles", "uint32_t", "[8]", len(unique), [w for t in unique for w in t]),
    emit_compressed(f, "arkanoid_tile_map", "unsigned short", "", TILE_COUNT, tile_map)))
write("arkanoid_palette.h", lambda f: emit_compressed(f, "arkanoid_rgb", "unsigned short", "", COLORS, rgb565))

sizes = {"rom": len(main), "tiles": len(unique) * 32, "tile_map": TILE_COUNT * 2, "rgb": COLORS * 2}
for k, v in sizes.items():
    print(f"  {k:8s} {v:6d}")
total = sum(sizes.values())
print(f"unique tiles {len(unique)} of {TILE_COUNT}")
print(f"Arena total {total} B (budget 182000)")
assert total < 182000
assert sizes["tiles"] + sizes["tile_map"] < LOCAL[0]   # one Arena block
