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


def _decode_tiles(rom):
    # gfx_8x8x4_packed_msb
    tiles = mame_decode(rom, 8, 8, step(0, 1, 4), step(0, 4, 8), step(0, 32, 8), 256, 512)
    return [pack_nibbles(row) for t in tiles for row in t]


def _decode_sprites(rom):
    # gfx_8x8x4_row_2x2_group_packed_msb
    xoffs = step(0, 4, 8) + step(256, 4, 8)
    yoffs = step(0, 32, 8) + step(512, 32, 8)
    sprites = mame_decode(rom, 16, 16, step(0, 1, 4), xoffs, yoffs, 1024, 512)
    return pack_sprite_rows(sprites)


def _palette(prom):
    return [rgb565_swapped(*prom_rgb_332(v)) for v in prom]


def _write_cmap(ctx, pal, tile_clut, spr_clut):
    def emit(f):
        f.write("// Green Beret color maps, RGB565 byte-swapped for the SPI display.\n\n")
        f.write("// Characters: 16 colors x 16 pens, lookup 577h11 -> palette 0x10 | nibble\n")
        write_color_tables(f, "gberet_tile", pal, tile_clut, 0x10, " PROGMEM")
        f.write("// Sprites: 16 colors x 16 pens, lookup 577h10 -> palette nibble\n")
        write_color_tables(f, "gberet_spr", pal, spr_clut, 0x00, " PROGMEM")

    write_asset(ctx, "gberet_cmap.h", None, emit, "gberet")


def _convert(ctx: Context) -> None:
    arena_block = layout.read_layouts(layout.header_path(ctx.paths))["local"][0]
    rom = rom_bytes_all(ctx, _MAIN)
    tiles = _decode_tiles(rom_bytes(ctx, _TILES))
    sprites = _decode_sprites(rom_bytes_all(ctx, _SPRITES))

    write_bytes(ctx, "gberet_rom.h", "gberet_rom", rom, "Z80 ROM 0x0000-0xbfff", "gberet")
    write_array(ctx, "gberet_tilemap.h", "gberet_tilemap", "uint32_t", "[8]", 512, tiles,
                "512 chars 8x8 4 bpp, row = 8 pens, pixel 0 in bits 31-28", "gberet")
    write_array(ctx, "gberet_spritemap.h", "gberet_spritemap", "uint32_t", "[16][2]", 512, sprites,
                "512 sprites 16x16 4 bpp, [row][0] pixels 0-7, [row][1] pixels 8-15, first in bits 31-28",
                "gberet")
    _write_cmap(ctx, _palette(rom_bytes(ctx, _PAL)), rom_bytes(ctx, _TILE_CLUT), rom_bytes(ctx, _SPR_CLUT))

    sizes = {"rom": len(rom), "tilemap": len(tiles) * 4, "spritemap": len(sprites) * 4}
    log_sizes(ctx, sizes)
    assert sizes["rom"] + sizes["tilemap"] <= arena_block and sizes["spritemap"] <= arena_block


JOB = Job(name="gberet", convert=_convert, machine="gberet", zips=("gberet.zip",))
