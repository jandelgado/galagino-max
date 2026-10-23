from collections.abc import Sequence

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.emit.asset import emit_compressed
from romconv.emit.write import module_path

_TILE_ROMS = ("dk3v.3n", "dk3v.3p")
_TILES = 512
_TILE_ROM_SIZE = 4096
_SPRITE_ROMS = ("dk3v.7c", "dk3v.7d", "dk3v.7e", "dk3v.7f")
_SPRITES = 256
_SPRITE_SIZE = 16


def _decode_tile(data0: Sequence[int], data1: Sequence[int]) -> list[list[int]]:
    """8x8 2bpp tile from two bit planes, transposed (90 degrees ccw + vertical flip)."""
    original = [[0] * 8 for _ in range(8)]
    for y in range(8):
        for x in range(8):
            bit0 = 1 if (data0[y] & (1 << (7 - x))) else 0
            bit1 = 2 if (data1[y] & (1 << (7 - x))) else 0
            original[y][x] = bit0 + bit1

    transformed = [[0] * 8 for _ in range(8)]
    for y in range(8):
        for x in range(8):
            transformed[x][y] = original[y][x]
    return transformed


def _tile_values(data: Sequence[Sequence[int]]) -> list[int]:
    """Rows as 16 bit values, 8 pixels of 2 bits each."""
    vals = []
    for y in range(8):
        val = 0
        for x in range(8):
            val = (val << 2) | data[y][x]
        vals.append(val)
    return vals


def convert_tilemap(ctx: Context) -> None:
    charmap_data_0, charmap_data_1 = (ctx.rom(name) for name in _TILE_ROMS)
    if len(charmap_data_0) != _TILE_ROM_SIZE or len(charmap_data_1) != _TILE_ROM_SIZE:
        raise ConversionError(f"dkong3: {' or '.join(_TILE_ROMS)} is not {_TILE_ROM_SIZE} bytes")

    chars = []
    for i in range(_TILES):
        chunk0 = charmap_data_0[i * 8:(i + 1) * 8]
        chunk1 = charmap_data_1[i * 8:(i + 1) * 8]
        chars.append(_decode_tile(chunk0, chunk1))

    with ctx.header("dkong3_tilemap.h") as f:
        f.write("// File generato automaticamente per la tilemap di Donkey Kong 3.\n")
        f.write("// I tile sono stati ruotati di 90° antiorario e flippati verticalmente.\n")
        f.write("// Dati estratti da: dkong3.zip: dk3v.3n, dk3v.3p\n\n")
        flat = [v for c in chars for v in _tile_values(c)]
        emit_compressed(f, "dkong3_tilemap", "unsigned short", "[8]", _TILES, flat)


def _decode_sprite(data_chunks: Sequence[Sequence[int]]) -> list[list[int]]:
    """16x16 2bpp sprite. Unflipped and unrotated: dkong3 flips at blit time."""
    sprite = []
    for y in range(16):
        row = []
        for x in range(16):
            # c0 and c1 are swapped relative to MAME's order
            c1 = 2 if data_chunks[y // 8 + 2][15 - x] & (0x80 >> (y & 7)) else 0
            c0 = 1 if data_chunks[y // 8][15 - x] & (0x80 >> (y & 7)) else 0
            row.append(c0 + c1)
        sprite.append(row)
    return sprite


def _sprite_values(data: Sequence[Sequence[int]]) -> list[int]:
    vals = []
    for pixel_row in data:
        val = 0
        for pixel_value in pixel_row:
            val = (val << 2) | pixel_value
        vals.append(val)
    return vals


def convert_sprites(ctx: Context) -> None:
    roms_data = [ctx.rom(name) for name in _SPRITE_ROMS]

    sprites = []
    for i in range(_SPRITES):
        sprite_offset = i * _SPRITE_SIZE
        data_chunks = [rom[sprite_offset:sprite_offset + _SPRITE_SIZE] for rom in roms_data]
        sprites.append(_decode_sprite(data_chunks))

    with ctx.header("dkong3_spritemap.h") as f:
        f.write(f"// File generato da {module_path(__file__)} (modifica: flip/rotazione per sprite)\n")
        f.write("// Dati da: dkong3.zip: dk3v.7c, dk3v.7d, dk3v.7e, dk3v.7f\n\n")
        flat = [v for s in sprites for v in _sprite_values(s)]
        emit_compressed(f, "dkong3_sprites", "uint32_t", "[16]", _SPRITES, flat)
