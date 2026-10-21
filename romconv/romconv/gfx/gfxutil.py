from collections.abc import Sequence


def mame_decode(data: Sequence[int], width: int, height: int, planes: Sequence[int], xoffs: Sequence[int],
                yoffs: Sequence[int], bits_per_tile: int, count: int, base_bit: int = 0) -> list[list[list[int]]]:
    """Generic MAME gfx decoder (planes/xoffs/yoffs as absolute OFFSET BIT); tiles[t][y][x]."""
    tiles = []
    for t in range(count):
        base = base_bit + t * bits_per_tile
        tile = []
        for y in range(height):
            row = []
            for x in range(width):
                v = 0
                for p in planes:
                    off = base + yoffs[y] + xoffs[x] + p
                    bit = (data[off >> 3] >> (7 - (off & 7))) & 1
                    v = (v << 1) | bit
                row.append(v)
            tile.append(row)
        tiles.append(tile)
    return tiles


def rot_galagino(tile: Sequence[Sequence[int]]) -> list[list[int]]:
    """90 deg rotation for galagino's portrait framebuffer."""
    n = len(tile)
    return [[tile[n - 1 - x][y] for x in range(n)] for y in range(n)]


def hex16(v: int) -> str:
    return "0x{:04x}".format(v & 0xFFFF)


def step(start: int, inc: int, n: int) -> list[int]:
    """MAME's STEPn(start, inc) offset list."""
    return [start + i * inc for i in range(n)]


def pack_nibbles(pens: Sequence[int]) -> int:
    """4 bit pens into one word, first pen in the highest nibble."""
    v = 0
    for p in pens:
        v = (v << 4) | p
    return v


def pack_pens(pens: Sequence[int], bits: int) -> int:
    """Pens into one word, first pen in the lowest `bits`."""
    return sum(p << (bits * i) for i, p in enumerate(pens))


def column_words(tiles: Sequence[Sequence[Sequence[int]]], bits: int) -> list[int]:
    """8x8 tiles[t][y][x] as one word per native x column: the pens of y = 0..7, y = 0 lowest."""
    return [pack_pens([t[y][x] for y in range(8)], bits) for t in tiles for x in range(8)]


def pack_sprite_rows(sprites: Sequence[Sequence[Sequence[int]]]) -> list[int]:
    """16x16 sprites[s][y][x] as two nibble words per row (x 0-7, x 8-15), first pen in the highest nibble."""
    return [pack_nibbles(row[h * 8:h * 8 + 8]) for s in sprites for row in s for h in range(2)]


def pack_pairs(sprites: Sequence[Sequence[Sequence[int]]]) -> list[int]:
    """16x16 sprites[s][y][x] as 8 bytes per row, low nibble = even x."""
    return [row[x] | (row[x + 1] << 4) for s in sprites for row in s for x in range(0, 16, 2)]
