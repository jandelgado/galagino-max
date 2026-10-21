from collections.abc import Sequence

from romconv.core.context import Context
from romconv.emit.asset import emit_compressed

# (pengo.zip name, name in older MAME sets, SHA-1)
_GFX_BANKS = (
    ("epr-1640.ic92", "ep1640.92", "e542bcc28f292be9a0a29d949de726e0b55e654a"),
    ("epr-1695.ic105", "ep1695.105", "bdec535e486b43a8f5550334beff423eeace10b2"),
)
_BANK_SIZE = 4096  # tiles in the first half of each bank, sprites in the second
_TILES = 512
_SPRITE_BYTES = 64

# MAME spritelayout: bit offsets of the pixels, the two planes 4 bits apart
_X_OFFSETS = (8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
              24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3, 0, 1, 2, 3)
_Y_OFFSETS = (0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8,
              32 * 8, 33 * 8, 34 * 8, 35 * 8, 36 * 8, 37 * 8, 38 * 8, 39 * 8)
_PLANE_OFFSETS = (0, 4)


def _rotate_cw(gfx_data: Sequence[Sequence[int]], width: int, height: int) -> list[list[int]]:
    rotated = [[0] * width for _ in range(height)]
    for y in range(height):
        for x in range(width):
            rotated[x][(height - 1) - y] = gfx_data[y][x]
    return rotated


def _decode_tile(raw_tile_data: Sequence[int]) -> list[list[int]]:
    tile_data_2d = [[0] * 8 for _ in range(8)]
    for y in range(8):
        byte_left = raw_tile_data[y + 8]
        byte_right = raw_tile_data[y]
        plane0_left = byte_left & 0x0F
        plane1_left = (byte_left >> 4) & 0x0F
        plane0_right = byte_right & 0x0F
        plane1_right = (byte_right >> 4) & 0x0F
        for i in range(4):
            bit0_l = (plane0_left >> i) & 1
            bit1_l = (plane1_left >> i) & 1
            tile_data_2d[y][3 - i] = (bit1_l << 1) | bit0_l
            bit0_r = (plane0_right >> i) & 1
            bit1_r = (plane1_right >> i) & 1
            tile_data_2d[y][7 - i] = (bit1_r << 1) | bit0_r
    return tile_data_2d


def _tile_values(data: Sequence[Sequence[int]]) -> list[int]:
    vals = []
    for y in range(8):
        val = 0
        for x in range(8):
            val = (val >> 2) | (data[y][x] << 14)
        vals.append(val)
    return vals


def convert_tiles(ctx: Context) -> None:
    rom1, rom2 = (ctx.rom(names[:2], names[2]) for names in _GFX_BANKS)

    tile_data_raw = rom1[:_BANK_SIZE] + rom2[:_BANK_SIZE]
    decoded_tiles = [_decode_tile(tile_data_raw[i * 16:(i + 1) * 16]) for i in range(_TILES)]
    final_tiles = [_rotate_cw(t, 8, 8) for t in decoded_tiles]

    with ctx.header("pengo_tiles.h") as f:
        flat = [v for t in final_tiles for v in _tile_values(t)]
        emit_compressed(f, "pengo_tiles", "unsigned short", "[8]", len(final_tiles), flat)


def _decode_sprite(raw_sprite_data: Sequence[int]) -> list[list[int]]:
    """MAME's algorithm; plane 0 is the color's bit 1, plane 1 its bit 0 (Pengo's color order)."""
    sprite_2d = [[0] * 16 for _ in range(16)]

    for y in range(16):
        for x in range(16):
            color = 0
            for plane in range(2):
                plane_bit_offset = _Y_OFFSETS[y] + _X_OFFSETS[x] + _PLANE_OFFSETS[plane]
                byte_index = plane_bit_offset // 8
                bit_index = 7 - (plane_bit_offset % 8)

                if (raw_sprite_data[byte_index] >> bit_index) & 1:
                    color |= (1 << (1 - plane))

            sprite_2d[y][x] = color

    return sprite_2d


def _sprite_values(sprite_data: Sequence[Sequence[int]]) -> list[int]:
    vals = []
    for y in range(16):
        val = 0
        for x in range(16):
            val = (val >> 2) | (sprite_data[y][x] << 30)
        vals.append(val)
    return vals


def convert_sprites(ctx: Context) -> None:
    rom1, rom2 = (ctx.rom(names[:2], names[2]) for names in _GFX_BANKS)

    sprite_data_raw = rom1[_BANK_SIZE:] + rom2[_BANK_SIZE:]
    num_sprites = len(sprite_data_raw) // _SPRITE_BYTES
    decoded_sprites = [_decode_sprite(sprite_data_raw[i * _SPRITE_BYTES:(i + 1) * _SPRITE_BYTES])
                       for i in range(num_sprites)]
    final_sprites = [_rotate_cw(s, 16, 16) for s in decoded_sprites]

    with ctx.header("pengo_spritemap.h") as f:
        # 2 banks x 64 sprites, unflipped: pengo::blit_sprite flips at run time
        flat = [v for s in final_sprites for v in _sprite_values(s)]
        emit_compressed(f, "pengo_sprites", "uint32_t", "[64][16]", 2, flat)
