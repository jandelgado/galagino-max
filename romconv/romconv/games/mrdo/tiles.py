from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.emit.asset import emit_compressed

_NUM_TILES = 512
_TILE_SIZE = 8
_ROM_SIZE = 4096


def _rotate_90_ccw(tile: list[list[int]]) -> list[list[int]]:
    new_tile = [[0] * _TILE_SIZE for _ in range(_TILE_SIZE)]
    for y in range(_TILE_SIZE):
        for x in range(_TILE_SIZE):
            new_tile[(_TILE_SIZE - 1) - x][y] = tile[y][x]
    return new_tile


def _convert_tiles(ctx: Context, rom_lsb: str, rom_msb: str, symbol: str, header: str, layer: str) -> None:
    """8x8 2bpp tiles of one layer from its LSB and MSB plane ROMs, rotated 90 degrees ccw and
    flipped horizontally."""
    plane0_data, plane1_data = ctx.rom(rom_lsb), ctx.rom(rom_msb)
    if len(plane0_data) != _ROM_SIZE or len(plane1_data) != _ROM_SIZE:
        raise ConversionError(f"mrdo: {rom_lsb} or {rom_msb} is not {_ROM_SIZE} bytes")

    tiles = []
    for tile_idx in range(_NUM_TILES):
        tile_data = []
        base_offset = tile_idx * _TILE_SIZE
        for y in range(_TILE_SIZE):
            row_data = []
            byte_p0 = plane0_data[base_offset + y]
            byte_p1 = plane1_data[base_offset + y]
            for x in range(_TILE_SIZE):
                bit_shift = 7 - x
                row_data.append((((byte_p1 >> bit_shift) & 1) << 1) | ((byte_p0 >> bit_shift) & 1))
            tile_data.append(row_data)
        tiles.append(tile_data)

    tiles = [_rotate_90_ccw(tile) for tile in tiles]
    tiles = [[row[::-1] for row in tile] for tile in tiles]

    with ctx.header(header) as f:
        f.write(f"// File generato da script. Contiene i {_NUM_TILES} tiles di {layer} (8x8, 2bpp) per Mr. Do! (Universal).\n")
        f.write("// Decodifica basata sulla definizione ufficiale del driver MAME.\n")
        f.write("// I tiles sono stati ruotati di 90 gradi in senso antiorario.\n")
        f.write("// I tiles sono stati flippati orizzontalmente.\n")
        f.write("#include <stdint.h>\n\n")
        flat = [v for tile in tiles for row in tile for v in row]
        emit_compressed(f, symbol, "unsigned char", "[%d][%d]" % (_TILE_SIZE, _TILE_SIZE), _NUM_TILES, flat)


def convert_bg_tiles(ctx: Context) -> None:
    _convert_tiles(ctx, "n8-07.bin", "r8-08.bin", "mrdo_bg_tiles", "mrdo_bg_tiles.h", "BACKGROUND")


def convert_fg_tiles(ctx: Context) -> None:
    _convert_tiles(ctx, "u8-10.bin", "s8-09.bin", "mrdo_fg_tiles", "mrdo_fg_tiles.h", "foreground")
