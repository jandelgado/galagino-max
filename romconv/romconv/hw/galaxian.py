"""Tile/sprite decode for Galaxian-family hardware (2bpp tile planes,
Frogger-style 16x16 sprite layout)."""
from collections.abc import Sequence

from romconv.gfx.sprites import parse_sprite_galaxian, sprite_row_values
from romconv.gfx.tiles import dump_chr_values, parse_chr_2


def convert_tiles(plane0: Sequence[int], plane1: Sequence[int]) -> list[list[int]]:
    tiles = []
    for t in range(len(plane0) // 8):
        tiles.append(dump_chr_values(parse_chr_2(plane0[t * 8:t * 8 + 8], plane1[t * 8:t * 8 + 8])))
    return tiles


def convert_sprites(plane0: Sequence[int], plane1: Sequence[int]) -> list[list[int]]:
    """Unflipped sprites, 16 row values each. The blitters flip at draw time."""
    sprites = []
    for s in range(len(plane0) // 32):
        sprites.append(sprite_row_values(parse_sprite_galaxian(plane0[32 * s:32 * (s + 1)],
                                                               plane1[32 * s:32 * (s + 1)])))
    return sprites
