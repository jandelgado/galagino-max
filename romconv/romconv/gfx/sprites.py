from collections.abc import Sequence
from types import MappingProxyType

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.emit.asset import emit_compressed, emit_plain
from romconv.gfx.bits import decode_data
from romconv.gfx.tiles import unscramble_anteater

# formats of the Pacman family 16x16 sprites; value: (pacman byte order, data bit swap)
_PACMAN_FAMILY = MappingProxyType({
    "pacman": (True, False), "galaga": (False, False), "digdug": (False, False), "eyes": (True, True),
    "lizwiz": (True, False), "mrtnt": (True, True), "crush": (True, False),
})


def _dump_sprite_4bpp_values(data: Sequence[Sequence[int]]) -> list[int]:
    vals = []
    for y in range(16):
        val = 0
        for x in range(16):
            val = (val >> 4) + (data[y][x] << (64 - 4))
        vals.append(val & 0xffffffff)
        vals.append((val >> 32) & 0xffffffff)
    return vals


def sprite_row_values(s: Sequence[Sequence[int]]) -> list[int]:
    # 2bpp sprites pack as uint32_t[16], one long per row (differs from
    # 1942's 4bpp _dump_sprite_4bpp_values).
    vals = []
    for y in range(16):
        val = 0
        for x in range(16):
            val = (val >> 2) + (s[y][x] << (32 - 2))
        vals.append(val & 0xffffffff)
    return vals


def parse_sprite_galaxian(data0: Sequence[int], data1: Sequence[int]) -> list[list[int]]:
    """16x16 sprite of the galaxian family (frogger without the D0/D1 swap):
    parts of each sprite are distributed over both ROMs."""
    sprite = []
    for y in range(16):
        row = []
        for x in range(16):
            ym = (y & 7) | ((x & 8) ^ 8)
            xm = (x & 7) | (y & 8)
            byte_idx = (xm ^ 7) + ((ym & 8) << 1)
            bit_mask = 0x80 >> (ym & 7)
            c0 = 1 if data0[byte_idx] & bit_mask else 0
            c1 = 2 if data1[byte_idx] & bit_mask else 0
            row.append(c0 + c1)
        sprite.append(row)
    return sprite


def _parse_sprite_frogger(data0: Sequence[int], data1: Sequence[int]) -> list[list[int]]:
    # in frogger D0/D1 of first rom are swapped
    swapped = [(b & 0xfc) | ((b & 1) << 1) | ((b & 2) >> 1) for b in data0]
    return parse_sprite_galaxian(swapped, data1)


def _parse_sprite_dkong(data: Sequence[Sequence[int]]) -> list[list[int]]:
    # dkong has parts of each sprite distributed over all four roms
    sprite = []
    for y in range(16):
        row = []
        for x in range(16):
            c0 = 1 if data[y // 8][15 - x] & (0x80 >> (y & 7)) else 0
            c1 = 2 if data[y // 8 + 2][15 - x] & (0x80 >> (y & 7)) else 0
            row.append(c0 + c1)
        sprite.append(row)
    return sprite


def _parse_sprite_1942(data_low: Sequence[int], data_high: Sequence[int]) -> list[list[int]]:
    sprite = []
    for y in range(16):
        row = []
        for x in range(16):
            byte = 2 * x + (((15 - y) & 4) >> 2) + (((15 - y) & 8) << 2)
            bit = (15 - y) & 3
            c0 = 1 if data_low[byte] & (0x80 >> bit) else 0
            c1 = 2 if data_low[byte] & (0x08 >> bit) else 0
            c2 = 4 if data_high[byte] & (0x80 >> bit) else 0
            c3 = 8 if data_high[byte] & (0x08 >> bit) else 0
            row.append(c0 + c1 + c2 + c3)
        sprite.append(row)
    return sprite


def _parse_sprite(data: Sequence[int], pacman_fmt: bool, decode: bool) -> list[list[int]]:
    # the pacman sprite format differs from the galaga
    # one. The top 4 pixels are in fact the bottom four
    # ones for pacman
    if decode:
        data = decode_data(data)

    sprite = []
    for y in range(16):
        row = []
        for x in range(16):
            idx = ((y & 8) << 1) + (((x & 8) ^ 8) << 2) + (7 - (x & 7)) + 2 * (y & 4)
            c0 = 1 if data[idx] & (0x08 >> (y & 3)) else 0
            c1 = 2 if data[idx] & (0x80 >> (y & 3)) else 0
            row.append(c0 + c1)
        sprite.append(row)

    if pacman_fmt:
        sprite = sprite[4:] + sprite[:4]
    return sprite


def _frogger_family(planes: Sequence[Sequence[int]], count: int) -> list[list[list[int]]]:
    # most of these aren't sprites but tiles. Converting them all
    # won't hurt as flash memory is no the limit
    return [_parse_sprite_frogger(planes[0][32 * s:32 * (s + 1)], planes[1][32 * s:32 * (s + 1)])
            for s in range(count)]


def spritemap(ctx: Context, symbol: str, fmt: str, roms: Sequence[str], header: str, *,
              compress: bool = False, plain: bool = False) -> None:
    """16x16 sprites of one machine family (fmt). 1942 (4bpp): COMPRESSED
    unless plain. Others (2bpp, stored unflipped, the blitters flip at draw
    time): PLAIN unless compress."""
    sprites: list[list[list[int]]] = []

    if fmt == "frogger":
        planes = [ctx.rom(name) for name in roms]
        if any(len(p) != 2048 for p in planes):
            raise ConversionError(f"{symbol}: spritemap ROMs are not 2048 bytes each")
        sprites = _frogger_family(planes, 64)

    elif fmt in _PACMAN_FAMILY:
        pacman_fmt, decode = _PACMAN_FAMILY[fmt]
        for name in roms:
            data = ctx.rom(name)
            if len(data) != 4096:
                raise ConversionError(f"{name}: spritemap ROM is not 4096 bytes")
            # read and parse all 64 sprites
            for s in range(64):
                sprites.append(_parse_sprite(data[64 * s:64 * (s + 1)], pacman_fmt, decode))

    elif fmt == "1942":
        half = len(roms) // 2
        for i in range(half):
            # 1942 uses 4bpp sprites, roms are used in pairs
            low, high = ctx.rom(roms[i]), ctx.rom(roms[i + half])
            for s in range(256):
                sprites.append(_parse_sprite_1942(low[64 * s:64 * (s + 1)], high[64 * s:64 * (s + 1)]))

    elif fmt == "anteater":
        data = unscramble_anteater([ctx.rom(name) for name in roms])
        sprites = _frogger_family([data[0:2048], data[2048:4096]], 64)

    elif fmt == "bagman":
        sprites = _frogger_family([ctx.rom(name) for name in roms], 128)

    elif fmt == "dkong":
        planes = [ctx.rom(name) for name in roms]
        if any(len(p) != 2048 for p in planes):
            raise ConversionError(f"{symbol}: spritemap ROMs are not 2048 bytes each")
        for s in range(128):
            sprites.append(_parse_sprite_dkong([p[16 * s:16 * (s + 1)] for p in planes[:4]]))

    else:
        raise ConversionError(f"{symbol}: unknown sprite format {fmt!r}")

    with ctx.header(header) as f:
        if fmt == "1942":
            rows = [_dump_sprite_4bpp_values(s) for s in sprites]
            if plain:
                body = ",\n".join("  { " + ",".join(hex(v) for v in r) + " }" for r in rows)
                emit_plain(f, symbol, "uint32_t", "[32]", len(sprites), body)
            else:
                emit_compressed(f, symbol, "uint32_t", "[32]", len(sprites), [v for r in rows for v in r])
        elif compress:
            flat = [v for s in sprites for v in sprite_row_values(s)]
            emit_compressed(f, symbol, "uint32_t", "[16]", len(sprites), flat)
        else:
            body = ",\n".join("  { " + ",".join(hex(v) for v in sprite_row_values(s)) + " }" for s in sprites)
            emit_plain(f, symbol, "uint32_t", "[16]", len(sprites), body)
