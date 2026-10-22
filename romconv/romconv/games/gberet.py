"""Convert the MAME gberet ROM set into galagino headers (MAME konami/gberet.cpp)."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.write import write_array, write_asset, write_bytes, write_color_tables
from romconv.gfx.color import prom_rgb_332, rgb565_swapped
from romconv.gfx.gfxutil import mame_decode, pack_nibbles, pack_sprite_rows, step
from romconv.gfx.rom import rom_bytes, rom_bytes_all
from romconv.plan import layout
from romconv.plan.budget import log_sizes

# MAME ROM_START( gberet )
_MAIN = (
    ("577l03.10c", "5c66de1403c5df5b6647bb37e26070ffd33590e8"),
    ("577l02.8c", "b76f3789f152198bf8a9a366378d664e683c6c9d"),
    ("577l01.7c", "90d1463e16b0f52c01078be044ce3672d4acebff"),
)
_TILES = ("577l07.3f", "54adba9ae086852902d78ab36039498aae50d7a9")
_SPRITES = (
    ("577l06.5e", "094004e70c05df8cd486d0854c258fa766e2925d"),
    ("577l05.4e", "5f2bcf2b702fe05f8a022b6284cb2d0a5b5f222f"),
    ("577l08.4f", "b565842edf09feeb2c4ac44ad58331757586b6aa"),
    ("577l04.3e", "cac053cab68cb420edd408ce032143db7abc29f5"),
)
_PAL = ("577h09.2f", "c0e8a01e63ed8cf20b33456b68890313b387ad23")
_TILE_CLUT = ("577h11.6f", "77cff7c9c8433f999a87776021935864cf9dccb4")
_SPR_CLUT = ("577h10.5f", "406b8dfe54e6176082005cc5545e79c098672547")


def decode_tiles(rom, count=512):
    # gfx_8x8x4_packed_msb
    tiles = mame_decode(rom, 8, 8, step(0, 1, 4), step(0, 4, 8), step(0, 32, 8), 256, count)
    return [pack_nibbles(row) for t in tiles for row in t]


def decode_sprites(rom, count=512):
    # gfx_8x8x4_row_2x2_group_packed_msb
    xoffs = step(0, 4, 8) + step(256, 4, 8)
    yoffs = step(0, 32, 8) + step(512, 32, 8)
    sprites = mame_decode(rom, 16, 16, step(0, 1, 4), xoffs, yoffs, 1024, count)
    return pack_sprite_rows(sprites)


def _palette(prom):
    return [rgb565_swapped(*prom_rgb_332(v)) for v in prom]


def write_cmap(ctx, prefix, title, pal, tile_clut, spr_clut, lut_names):
    def emit(f):
        f.write(f"// {title} color maps, RGB565 byte-swapped for the SPI display.\n\n")
        f.write(f"// Characters: 16 colors x 16 pens, lookup {lut_names[0]} -> palette 0x10 | nibble\n")
        write_color_tables(f, f"{prefix}_tile", pal, tile_clut, 0x10, " PROGMEM")
        f.write(f"// Sprites: 16 colors x 16 pens, lookup {lut_names[1]} -> palette nibble\n")
        write_color_tables(f, f"{prefix}_spr", pal, spr_clut, 0x00, " PROGMEM")

    write_asset(ctx, f"{prefix}_cmap.h", None, emit, __file__)


def convert_set(ctx: Context, prefix: str, title: str, files: dict, rom_comment: str,
                lut_names: tuple[str, str], check_arena: bool = True) -> None:
    """Shared by the gberet board family; files: main, tiles, sprites, pal, tile_clut, spr_clut."""
    rom = rom_bytes_all(ctx, files["main"])
    tiles = decode_tiles(rom_bytes(ctx, files["tiles"]))
    sprites = decode_sprites(rom_bytes_all(ctx, files["sprites"]))

    write_bytes(ctx, f"{prefix}_rom.h", f"{prefix}_rom", rom, rom_comment, __file__)
    write_array(ctx, f"{prefix}_tilemap.h", f"{prefix}_tilemap", "uint32_t", "[8]", 512, tiles,
                "512 chars 8x8 4 bpp, row = 8 pens, pixel 0 in bits 31-28", __file__)
    write_array(ctx, f"{prefix}_spritemap.h", f"{prefix}_spritemap", "uint32_t", "[16][2]", 512, sprites,
                "512 sprites 16x16 4 bpp, [row][0] pixels 0-7, [row][1] pixels 8-15, first in bits 31-28",
                __file__)
    write_cmap(ctx, prefix, title, _palette(rom_bytes(ctx, files["pal"])), rom_bytes(ctx, files["tile_clut"]),
                rom_bytes(ctx, files["spr_clut"]), lut_names)

    sizes = {"rom": len(rom), "tilemap": len(tiles) * 4, "spritemap": len(sprites) * 4}
    log_sizes(ctx, sizes)
    if check_arena:
        arena_block = layout.read_layouts(layout.header_path(ctx.paths))["local"][0]
        assert sizes["rom"] + sizes["tilemap"] <= arena_block and sizes["spritemap"] <= arena_block


def _convert(ctx: Context) -> None:
    convert_set(ctx, "gberet", "Green Beret",
                dict(main=_MAIN, tiles=_TILES, sprites=_SPRITES, pal=_PAL, tile_clut=_TILE_CLUT,
                     spr_clut=_SPR_CLUT),
                "Z80 ROM 0x0000-0xbfff", ("577h11", "577h10"))


JOB = Job(name="gberet", convert=_convert, machine="gberet", zips=("gberet.zip",))
