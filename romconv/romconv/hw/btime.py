"""Gfx decoding of the Data East burger time board (Burger Time, Bump 'n' Jump, Scrambled Egg).

Layouts of MAME btime.cpp: gfx_8x8x3_planar chars and tile16layout sprites/backgrounds, three bit
planes that are equal thirds of the ROM region, first plane = MSB. Tiles are rotated 90 degrees for
galagino's portrait framebuffer."""
from romconv.gfx.gfxutil import mame_decode, rot_galagino

_CHAR_XOFFS = tuple(range(8))
_CHAR_YOFFS = tuple(y * 8 for y in range(8))
_CHAR_BITS_PER_TILE = 8 * 8

# sprites and backgrounds: xoffset split (right half first)
_TILE16_XOFFS = tuple([16 * 8 + x for x in range(8)] + list(range(8)))
_TILE16_YOFFS = tuple(y * 8 for y in range(16))
_TILE16_BITS_PER_TILE = 32 * 8


def _planes3(region_bits: int) -> list[int]:
    return [2 * (region_bits // 3), 1 * (region_bits // 3), 0 * (region_bits // 3)]


def decode_chars(region: bytes) -> tuple[list[list[list[int]]], int]:
    """(rotated 8x8 tiles, count) of a 3 plane region."""
    bits = len(region) * 8
    count = bits // 3 // _CHAR_BITS_PER_TILE
    tiles = mame_decode(region, 8, 8, _planes3(bits), _CHAR_XOFFS, _CHAR_YOFFS, _CHAR_BITS_PER_TILE, count)
    return [rot_galagino(t) for t in tiles], count


def decode_tiles16(region: bytes) -> tuple[list[list[list[int]]], int]:
    """(rotated 16x16 tiles, count) of a 3 plane region."""
    bits = len(region) * 8
    count = bits // 3 // _TILE16_BITS_PER_TILE
    tiles = mame_decode(region, 16, 16, _planes3(bits), _TILE16_XOFFS, _TILE16_YOFFS,
                        _TILE16_BITS_PER_TILE, count)
    return [rot_galagino(t) for t in tiles], count
