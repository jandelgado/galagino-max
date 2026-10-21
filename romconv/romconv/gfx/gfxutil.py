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


def hex8(v: int) -> str:
    return "0x{:02x}".format(v & 0xFF)


def hex16(v: int) -> str:
    return "0x{:04x}".format(v & 0xFFFF)


def hex32(v: int) -> str:
    return "0x{:08x}".format(v & 0xFFFFFFFF)
