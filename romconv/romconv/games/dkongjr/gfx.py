from collections.abc import Sequence

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.emit.asset import emit_compressed

_TILE_ROMS = ("v_3na.bin", "v_3pa.bin")
_TILE_ROM_SIZE = 4096
_TILES = 512
_SPRITE_ROMS = ("v_7c.bin", "v_7d.bin", "v_7e.bin", "v_7f.bin")
_SPRITE_ROM_SIZE = 2048
_SPRITES = 128


def _decode_tile(data0: Sequence[int], data1: Sequence[int]) -> list[list[int]]:
    """8x8 2bpp tile from two bit planes."""
    char = []
    for y in range(8):
        row = []
        for x in range(8):
            c0 = 1 if data0[7 - x] & (0x80 >> y) else 0
            c1 = 2 if data1[7 - x] & (0x80 >> y) else 0
            row.append(c0 + c1)
        char.append(row)
    return char


def _tile_values(data: Sequence[Sequence[int]]) -> list[int]:
    """Rows as 16 bit values, first pixel in the low bits."""
    vals = []
    for y in range(8):
        val = 0
        for x in range(8):
            val = (val >> 2) | (data[y][x] << 14)
        vals.append(val)
    return vals


def convert_tilemap(ctx: Context) -> None:
    charmap_data_0, charmap_data_1 = (ctx.rom(name) for name in _TILE_ROMS)
    if len(charmap_data_0) != _TILE_ROM_SIZE or len(charmap_data_1) != _TILE_ROM_SIZE:
        raise ConversionError(f"dkongjr: {' or '.join(_TILE_ROMS)} is not {_TILE_ROM_SIZE} bytes")

    chars = []
    for i in range(_TILES):
        chars.append(_decode_tile(charmap_data_0[8 * i:8 * (i + 1)], charmap_data_1[8 * i:8 * (i + 1)]))

    with ctx.header("dkongjr_tilemap.h") as f:
        f.write("// File generato automaticamente per la tilemap di Donkey Kong Jr.\n")
        f.write("// Dati estratti da: dkongjrj.zip: v_3na.bin, v_3pa.bin\n\n")
        flat = [v for c in chars for v in _tile_values(c)]
        emit_compressed(f, "dkongjr_tilemap", "unsigned short", "[8]", _TILES, flat)


def _decode_sprite(data_chunks: Sequence[Sequence[int]]) -> list[list[int]]:
    """16x16 2bpp sprite from four 16 byte chunks, as in Donkey Kong."""
    sprite = []
    for y in range(16):
        row = []
        for x in range(16):
            c0 = 1 if data_chunks[y // 8][15 - x] & (0x80 >> (y & 7)) else 0
            c1 = 2 if data_chunks[y // 8 + 2][15 - x] & (0x80 >> (y & 7)) else 0
            row.append(c0 + c1)
        sprite.append(row)
    return sprite


def _sprite_values(data: Sequence[Sequence[int]]) -> list[int]:
    """Rows as 32 bit values, 16 pixels of 2 bits each."""
    vals = []
    for y in range(16):
        val = 0
        for x in range(16):
            val = (val << 2) | data[y][x]
        vals.append(val)
    return vals


def convert_sprites(ctx: Context) -> None:
    spritemap_data = []
    for name in _SPRITE_ROMS:
        data = ctx.rom(name)
        if len(data) != _SPRITE_ROM_SIZE:
            raise ConversionError(f"dkongjr: {name} is not {_SPRITE_ROM_SIZE} bytes")
        spritemap_data.append(data)

    sprites = []
    for i in range(_SPRITES):
        data_chunks = [rom[16 * i:16 * (i + 1)] for rom in spritemap_data]
        sprites.append(_decode_sprite(data_chunks))

    with ctx.header("dkongjr_spritemap.h") as f:
        f.write("// File generato automaticamente per gli sprite di Donkey Kong Jr.\n")
        f.write("// Dati estratti da: dkongjrj.zip: v_7c.bin, v_7d.bin, v_7e.bin, "
                "v_7f.bin\n\n")
        # unflipped: dkongjr::blit_sprite flips at run time
        flat = [v for s in sprites for v in _sprite_values(s)]
        emit_compressed(f, "dkongjr_sprites", "uint32_t", "[16]", _SPRITES, flat)
