# Arkanoid (Taito 1986), MAME taito/arkanoid.cpp, set arkangc (Game
# Corporation bootleg, no MCU). ROT90: native 256x224 -> panel 224x256.
# Tiles: 8x8 3bpp, 4096 codes (2 gfx banks of 2048), deduplicated: the
#   map turns a native code into an index of the unique tiles. Rotated for
#   the portrait panel (rot_galagino): word[r] = native column r, pixel x
#   (panel column) in bits 31-28 - 4x. Sprites use the same tiles.
# Palette: 512 RGB565 from 3 PROMs (MAME RGB_444_PROMS weights), pen = color * 8 + pixel.
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.gfx.color import prom_level_4bit, rgb565_swapped
from romconv.gfx.gfxutil import mame_decode, rot_galagino
from romconv.plan import layout

_ROM_SIZE = 0xC000   # 0000-bfff mapped; second half of arkgc.2 unused
_TILE_COUNT, _COLORS = 4096, 512


def _pack(tile):
    return tuple(sum(p << (28 - 4 * x) for x, p in enumerate(row)) for row in rot_galagino(tile))


def _convert(ctx: Context) -> None:
    arena_block = layout.read_layouts(layout.header_path(ctx.paths))["local"][0]
    main = (ctx.rom("arkangc/arkgc.1", "beb759cee68009a06824b755d2aa26d7d436b5b0") +
            ctx.rom("arkangc/arkgc.2", "731c9224616a338084edd6944c754d68eabba7f2"))[:_ROM_SIZE]
    gfx = (ctx.rom("a75__03.ic64", "ac053cc4908b4075f918748b89570e07a0ba5116") +
           ctx.rom("a75__04.ic63", "5d253c46ccf4cd2976a5fb8b8713f0f345443d06") +
           ctx.rom("a75__05.ic62", "7520dd48de20db60a2038f134dcaa454988e7874"))
    prom = (ctx.rom("a75-07.ic24", "6bc589e8a609b4cf450aebedc8ce02d5d45c970f") +
            ctx.rom("a75-08.ic23", "c14f56b8ef103600862e7930709d293b0aa97a73") +
            ctx.rom("a75-09.ic22", "adaa003dcd981576ea1cc5f697d709b2d6b2ea29"))

    # MAME charlayout: planes {2*4096*64, 4096*64, 0}, first plane = MSB
    tiles = mame_decode(gfx, 8, 8, [2 * _TILE_COUNT * 64, _TILE_COUNT * 64, 0], list(range(8)),
                        [8 * i for i in range(8)], 64, _TILE_COUNT)

    unique, index, tile_map = [], {}, []
    for t in tiles:
        key = _pack(t)
        if key not in index:
            index[key] = len(unique)
            unique.append(key)
        tile_map.append(index[key])

    # MAME palette_device::palette_init_rgb_444_proms: resistor weights per bit
    rgb565 = [rgb565_swapped(prom_level_4bit(prom[i]), prom_level_4bit(prom[_COLORS + i]),
                             prom_level_4bit(prom[2 * _COLORS + i]))
              for i in range(_COLORS)]

    with ctx.header("arkanoid_rom.h") as f:
        emit_compressed(f, "arkanoid_rom", "unsigned char", "", len(main), main)
    ctx.log.info("wrote arkanoid_rom.h")
    with ctx.header("arkanoid_tiles.h") as f:
        emit_compressed(f, "arkanoid_tiles", "uint32_t", "[8]", len(unique), [w for t in unique for w in t])
        emit_compressed(f, "arkanoid_tile_map", "unsigned short", "", _TILE_COUNT, tile_map)
    ctx.log.info("wrote arkanoid_tiles.h")
    with ctx.header("arkanoid_palette.h") as f:
        emit_compressed(f, "arkanoid_rgb", "unsigned short", "", _COLORS, rgb565)
    ctx.log.info("wrote arkanoid_palette.h")

    sizes = {"rom": len(main), "tiles": len(unique) * 32, "tile_map": _TILE_COUNT * 2, "rgb": _COLORS * 2}
    for k, v in sizes.items():
        ctx.log.info(f"  {k:8s} {v:6d}")
    total = sum(sizes.values())
    ctx.log.info(f"unique tiles {len(unique)} of {_TILE_COUNT}")
    ctx.log.info(f"Arena total {total} B (budget 182000)")
    assert total < 182000
    assert sizes["tiles"] + sizes["tile_map"] < arena_block   # one Arena block


JOB = Job(name="arkanoid", convert=_convert, machine="arkanoid", zips=("arkanoid.zip",))
