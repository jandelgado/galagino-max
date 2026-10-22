from romconv.games import docastle
from romconv.gfx.color import rgb565_swapped


def test_tile_is_rotated_270_with_left_pixel_at_the_bottom():
    data = bytearray(32)
    data[0] = 0x5a           # native row 0: x 0 = 5, x 1 = 10
    tile = docastle._decode(data, 8)
    # panel row 7 - x, panel column y, nibble c = column c
    assert tile[7][0] == 5
    assert tile[6][0] == 10
    assert sum(tile[r][c] for r in range(8) for c in range(8)) == 15


def test_sprite_row_15_is_native_column_0():
    data = bytearray(128)
    data[8 * 3] = 0xf0       # native row 3, x 0 = 15
    data[8 * 9 + 7] = 0x01   # native row 9, x 15 = 1
    spr = docastle._decode(data, 16)
    assert spr[15][3] == 15
    assert spr[0][9] == 1


def test_pack_rows_puts_column_0_in_the_low_nibble():
    assert docastle._pack_rows([[1, 2, 3, 4, 5, 6, 7, 8]]) == [0x87654321]
    assert docastle._pack_rows([list(range(16))]) == [0x76543210, 0xfedcba98]


def test_palette_resistor_weights():
    pal = docastle._palette(bytes([0x00, 0xff, 0x20, 0x01, 0x04, 0x02]))
    assert pal[0] == rgb565_swapped(0, 0, 0)
    assert pal[1] == rgb565_swapped(0xff, 0xff, 0xff)
    assert pal[2] == rgb565_swapped(0x23, 0, 0)
    assert pal[3] == rgb565_swapped(0, 0, 0x52)
    assert pal[4] == rgb565_swapped(0, 0x23, 0)
    assert pal[5] == rgb565_swapped(0, 0, 0xad)
