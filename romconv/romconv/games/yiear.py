"""Convert the MAME yiear ROM set into galagino headers (MAME konami/yiear.cpp)."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.write import write_array, write_asset, write_bytes
from romconv.gfx.bits import BIT
from romconv.gfx.color import rgb565_swapped
from romconv.gfx.gfxutil import mame_decode, pack_nibbles, pack_sprite_rows, step
from romconv.gfx.rom import rom_bytes, rom_bytes_all
from romconv.plan import layout
from romconv.plan.budget import log_sizes


# MAME ROM_START( yiear ), version I
_MAIN = (
    ("407_i08.10d", "1b192130b5cd879ab686a21aa2b518c90edd89aa"),
    ("407_i07.8d", "d604a995a5505251904447ad697fc9e7f94bf241"),
)
_TILES = (
    ("407_c01.6h", "c267931d69794c292b7ebae5bc35ad842194683a"),
    ("407_c02.7h", "a2fd10bddfa4e95e9d49892737ace146209bfa2b"),
)
_SPRITES = (
    ("407_d05.16h", "0794935b490497b21b99045c90231b7bac151d42"),
    ("407_d06.17h", "5f2a4983b20251c712358547a7c62c0331c6cb6f"),
    ("407_d03.14h", "c5757d16c28f5966fd04675c0c640ef9b6b76ca5"),
    ("407_d04.15h", "555ba18a9648681e5140b3fd84af16959ee5296d"),
)
_PROM = ("407c10.1g", "10cd39f4e951ba6ca5610081c8c1fcd9d68b34d2")
_VLM = ("407_c09.8b", "f139f6cb41351eb81ee47d777db03012aa5fadb1")

_TILE_COUNT = 512
_SPRITE_COUNT = 512


def _decode_tiles(rom):
    # charlayout: planes {4, 0, half+4, half+0}, 16 bytes per char
    half = len(rom) * 4
    planes = [4, 0, half + 4, half]
    xoffs = step(0, 1, 4) + step(64, 1, 4)
    tiles = mame_decode(rom, 8, 8, planes, xoffs, step(0, 8, 8), 128, _TILE_COUNT)
    return [pack_nibbles(row) for t in tiles for row in t]


def _decode_sprites(rom):
    # spritelayout: 4 columns of 4 px, 64 bytes per sprite
    half = len(rom) * 4
    planes = [4, 0, half + 4, half]
    xoffs = [64 * c + i for c in range(4) for i in range(4)]
    yoffs = step(0, 8, 8) + step(256, 8, 8)
    sprites = mame_decode(rom, 16, 16, planes, xoffs, yoffs, 512, _SPRITE_COUNT)
    return pack_sprite_rows(sprites)


def _palette(prom):
    out = []
    for v in prom:
        r = 0x21 * BIT(v, 0) + 0x47 * BIT(v, 1) + 0x97 * BIT(v, 2)
        g = 0x21 * BIT(v, 3) + 0x47 * BIT(v, 4) + 0x97 * BIT(v, 5)
        b = 0x52 * BIT(v, 6) + 0xad * BIT(v, 7)
        out.append(rgb565_swapped(r, g, b))
    return out


def _write_palette(ctx, pal):
    write_asset(ctx, "yiear_palette.h", "PROM 407c10, RGB565 byte-swapped: sprites 0-15, tiles 16-31",
                lambda out: out.write("const unsigned short yiear_palette[32] = {\n  "
                                      + ", ".join(f"0x{c:04x}" for c in pal) + "\n};\n"), __file__)


def _convert(ctx: Context) -> None:
    arena_capacity = sum(layout.read_layouts(layout.header_path(ctx.paths))["local"])
    rom = rom_bytes_all(ctx, _MAIN)
    vlm = rom_bytes(ctx, _VLM)
    tiles = _decode_tiles(rom_bytes_all(ctx, _TILES))
    sprites = _decode_sprites(rom_bytes_all(ctx, _SPRITES))

    write_bytes(ctx, "yiear_rom.h", "yiear_rom", rom, "6809 ROM 0x8000-0xffff", __file__)
    write_bytes(ctx, "yiear_vlm.h", "yiear_vlm", vlm, "VLM5030 speech ROM", __file__)
    write_array(ctx, "yiear_tilemap.h", "yiear_tilemap", "uint32_t", "[8]", _TILE_COUNT, tiles,
                "512 chars 8x8 4 bpp, row = 8 pens, pixel 0 in bits 31-28", __file__)
    write_array(ctx, "yiear_spritemap.h", "yiear_spritemap", "uint32_t", "[16][2]", _SPRITE_COUNT, sprites,
                "512 sprites 16x16 4 bpp, [row][0] pixels 0-7, [row][1] pixels 8-15, first in bits 31-28",
                __file__)
    _write_palette(ctx, _palette(rom_bytes(ctx, _PROM)))

    log_sizes(ctx, {"rom": len(rom), "vlm": len(vlm), "tilemap": len(tiles) * 4, "spritemap": len(sprites) * 4},
              arena_capacity)


JOB = Job(name="yiear", convert=_convert, machine="yiear", zips=("yiear.zip",))
