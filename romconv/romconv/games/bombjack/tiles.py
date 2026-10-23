from collections.abc import Sequence

from romconv.core.context import Context
from romconv.emit.asset import emit_compressed

_BG_ROMS = ("06_l08t.bin", "07_n08t.bin", "08_r08t.bin")
_BG_TILES = 256
_BG_SIZE = 16
_FG_ROMS = ("03_e08t.bin", "04_h08t.bin", "05_k08t.bin")
_FG_TILES = 512
_FG_SIZE = 8


def _decode_bg_tile(data0: Sequence[int], data1: Sequence[int], data2: Sequence[int]) -> list[list[int]]:
    """16x16 3bpp tile; data0/1/2 are the planes of 06_l08t, 07_n08t, 08_r08t (bm0 = MSB)."""
    matrix = [[0] * _BG_SIZE for _ in range(_BG_SIZE)]

    for y in range(_BG_SIZE):
        row_byte_offset = (y % 8) + (16 if y >= 8 else 0)

        bm0 = (data0[row_byte_offset] << 8) | data0[row_byte_offset + 8]
        bm1 = (data1[row_byte_offset] << 8) | data1[row_byte_offset + 8]
        bm2 = (data2[row_byte_offset] << 8) | data2[row_byte_offset + 8]

        for x in range(_BG_SIZE):
            bit_pos = 15 - x
            bit_from_bm0 = 1 if (bm0 & (1 << bit_pos)) else 0
            bit_from_bm1 = 1 if (bm1 & (1 << bit_pos)) else 0
            bit_from_bm2 = 1 if (bm2 & (1 << bit_pos)) else 0
            matrix[y][x] = (bit_from_bm0 * 4) + (bit_from_bm1 * 2) + bit_from_bm2

    return matrix


def _bg_row_words(row_data: Sequence[int]) -> tuple[int, int]:
    """A row of 16 pixels as two uint32_t."""
    val1 = 0
    for x in range(8):
        val1 = (val1 << 3) | row_data[x]
    val2 = 0
    for x in range(8, 16):
        val2 = (val2 << 3) | row_data[x]
    return val1, val2


def convert_bg_tiles(ctx: Context) -> None:
    plane_bm0, plane_bm1, plane_bm2 = (ctx.rom(name) for name in _BG_ROMS)

    chars = []
    tile_byte_size = (_BG_SIZE * _BG_SIZE) // 8
    for i in range(_BG_TILES):
        base_offset = i * tile_byte_size
        chunk0 = plane_bm0[base_offset:base_offset + tile_byte_size]
        chunk1 = plane_bm1[base_offset:base_offset + tile_byte_size]
        chunk2 = plane_bm2[base_offset:base_offset + tile_byte_size]
        chars.append(_decode_bg_tile(chunk0, chunk1, chunk2))

    with ctx.header("bombjack_bg_tiles.h") as f:
        f.write("// File generato automaticamente per il background di Bomb Jack.\n")
        f.write("// Dati decodificati seguendo la logica C funzionante.\n")
        flat = []
        for c in chars:
            for y in range(_BG_SIZE):
                flat.extend(_bg_row_words(c[y]))
        emit_compressed(f, "bombjack_bg_tiles", "uint32_t", "[%d]" % (_BG_SIZE * 2), _BG_TILES, flat)


def _decode_fg_tile(plane_data: Sequence[Sequence[int]], tile_index: int) -> list[list[int]]:
    """8x8 3bpp tile; plane_data = [03_e08t (MSB), 04_h08t, 05_k08t (LSB)]."""
    char_matrix = [[0] * _FG_SIZE for _ in range(_FG_SIZE)]
    offset = tile_index * 8

    chunk0 = plane_data[0][offset:offset + 8]
    chunk1 = plane_data[1][offset:offset + 8]
    chunk2 = plane_data[2][offset:offset + 8]

    for y in range(_FG_SIZE):
        for x in range(_FG_SIZE):
            bit_pos = 7 - x
            bit2 = 4 if (chunk0[y] & (1 << bit_pos)) else 0
            bit1 = 2 if (chunk1[y] & (1 << bit_pos)) else 0
            bit0 = 1 if (chunk2[y] & (1 << bit_pos)) else 0
            char_matrix[y][x] = bit0 + bit1 + bit2

    return char_matrix


def _rotate_90_cw(matrix: Sequence[Sequence[int]]) -> list[list[int]]:
    rotated = [[0] * _FG_SIZE for _ in range(_FG_SIZE)]
    for y in range(_FG_SIZE):
        for x in range(_FG_SIZE):
            rotated[y][x] = matrix[(_FG_SIZE - 1) - x][y]
    return rotated


def _fg_row_value(row: Sequence[int]) -> int:
    val = 0
    for pixel in row:
        val = (val << 3) | pixel
    return val


def convert_fg_tiles(ctx: Context) -> None:
    plane_data = [ctx.rom(name) for name in _FG_ROMS]

    chars = [_rotate_90_cw(_decode_fg_tile(plane_data, i)) for i in range(_FG_TILES)]

    with ctx.header("bombjack_fg_tiles.h") as f:
        f.write("// File generato automaticamente per Bomb Jack (formato uint32_t).\n")
        f.write("// Ordine dei bit-plane corretto per i colori.\n\n")
        flat = [_fg_row_value(row) for c in chars for row in c]
        emit_compressed(f, "bombjack_fg_tiles", "uint32_t", "[%d]" % _FG_SIZE, _FG_TILES, flat)
