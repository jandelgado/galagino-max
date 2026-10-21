from collections.abc import Sequence

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.header import HeaderWriter
from romconv.emit.asset import emit_compressed, emit_plain
from romconv.gfx.bits import BIT, decode_data

_CHAR_PLAIN = "const unsigned short {}[][8] = {{"
# symbols whose character ROM goes through the Pacman family data bit swap
_DECODED = ("eyes_tilemap", "mrtnt_tilemap")


def dump_chr_values(data: Sequence[Sequence[int]]) -> list[int]:
    vals = []
    for y in range(8):
        val = 0
        for x in range(8):
            val = (val >> 2) + (data[y][x] << (16 - 2))
        vals.append(val & 0xFFFF)
    return vals


def _dump_chr(data: Sequence[Sequence[int]]) -> str:
    return ",".join(hex(v) for v in dump_chr_values(data))


# 1942 can flip background tiles
def _dump_tile_1942(data: Sequence[Sequence[int]]) -> str:
    hexs = []
    for y in range(16):
        val = 0
        for x in range(16):
            val = (val >> 4) + (data[y][x] << (64 - 4))
        hexs.append(hex(val & 0xffffffff))
        hexs.append(hex(val >> 32))
    return ",".join(hexs)


def parse_chr_2(data0: Sequence[int], data1: Sequence[int]) -> list[list[int]]:
    """8x8 character from two plane ROMs (galaxian family)."""
    char = []
    for y in range(8):
        row = []
        for x in range(8):
            c0 = 1 if data0[7 - x] & (0x80 >> y) else 0
            c1 = 2 if data1[7 - x] & (0x80 >> y) else 0
            row.append(c0 + c1)
        char.append(row)
    return char


def parse_chr(data: Sequence[int]) -> list[list[int]]:
    char = []
    for y in range(8):
        row = []
        for x in range(8):
            byte = data[15 - x - 2 * (y & 4)]
            c0 = 1 if byte & (0x08 >> (y & 3)) else 0
            c1 = 2 if byte & (0x80 >> (y & 3)) else 0
            row.append(c0 + c1)
        char.append(row)
    return char


def _parse_chr_dd(data: Sequence[int]) -> list[list[int]]:
    char = []
    for y in range(8):
        row = []
        for x in range(8):
            byte = data[7 - x]
            row.append(3 if byte & (0x01 << y) else 0)
        char.append(row)
    return char


def _parse_chr_1942(data: Sequence[int]) -> list[list[int]]:
    char = []
    for y in range(8):
        row = []
        for x in range(8):
            byte = data[2 * x + 1] if y < 4 else data[2 * x]
            c0 = 1 if byte & (0x10 << (y & 3)) else 0
            c1 = 2 if byte & (0x01 << (y & 3)) else 0
            row.append(c0 + c1)
        char.append(row)
    return char


def _write_chars(f: HeaderWriter, symbol: str, chars: Sequence[Sequence[Sequence[int]]], compress: bool) -> None:
    if compress:
        flat = [v for c in chars for v in dump_chr_values(c)]
        emit_compressed(f, symbol, "unsigned short", "[8]", len(chars), flat)
    else:
        print(_CHAR_PLAIN.format(symbol), file=f)
        print(",\n".join(" { " + _dump_chr(c) + " }" for c in chars), file=f)
        print("};", file=f)


def charmap(ctx: Context, symbol: str, rom: str, header: str, *, compress: bool = False,
            plain: bool = False) -> None:
    """Character set of one ROM: 1942/commando (8 or 16 KB), galaga and
    pacman (4 KB), else digdug. plain: 1942 format PLAIN, else COMPRESSED."""
    # The character map rom contains the same set of 128 characters
    # two times. The second set is upside down for cocktail mode. We
    # ignore that.
    data = ctx.rom(rom)

    # 1942 8K (512 chars), commando 16K (1024 chars)
    is_1942 = len(data) in (8192, 16384)

    chars = []
    if is_1942:
        for i in range(len(data) // 16):
            chars.append(_parse_chr_1942(data[16 * i:16 * (i + 1)]))
    elif len(data) == 4096:
        # galaga and pacman 2bpp format
        if symbol in _DECODED:
            data = decode_data(data)
        for i in range(256):
            chars.append(parse_chr(data[16 * i:16 * (i + 1)]))
    else:
        # digdug format
        for i in range(128):
            chars.append(_parse_chr_dd(data[8 * i:8 * (i + 1)]))

    with ctx.header(header) as f:
        if is_1942 and plain:
            body = ",\n".join(" { " + ",".join(hex(v) for v in dump_chr_values(c)) + " }" for c in chars)
            emit_plain(f, symbol, "unsigned short", "[8]", len(chars), body)
        else:
            _write_chars(f, symbol, chars, compress or is_1942)


def _parse_tile_1942(b0: Sequence[int], b1: Sequence[int], b2: Sequence[int]) -> list[list[int]]:
    tile = []
    for y in range(16):
        row = []
        for x in range(16):
            bit = 1 << (y & 7)
            byte = x + (0 if (y // 8) else 16)
            p0 = 4 if (b0[byte] & bit) else 0
            p1 = 2 if (b1[byte] & bit) else 0
            p2 = 1 if (b2[byte] & bit) else 0
            row.append(p0 + p1 + p2)
        tile.append(row)
    return tile


def tilemap_1942(ctx: Context, symbol: str, roms: tuple[tuple[str, str], ...], header: str, *,
                 compress: bool = False, split: int = 0, plain: bool = False) -> None:
    """1942 16x16 tiles; one pair of ROMs per colour bit plane. split N: two
    assets <symbol>_a (tiles 0..N-1) and <symbol>_b, a PLAIN if plain."""
    data: list[list[int]] = [[], [], []]
    for plane, names in zip(data, roms):
        for name in names:
            plane.extend(ctx.rom(name))

    # 3 * 16k bytes = 128k pixel @ 3bpp, 256 pixel per tile: 512 tiles
    tiles = []
    for t in range(len(data[0]) * 8 // 256):
        tiles.append(_parse_tile_1942(data[0][t * 32:32 + t * 32], data[1][t * 32:32 + t * 32],
                                      data[2][t * 32:32 + t * 32]))

    # Unflipped tiles only: 1942.cpp flips at blit time, 1/4 the size.
    tiles_str = [_dump_tile_1942(t) for t in tiles]
    with ctx.header(header) as f:
        if split:
            # two assets <id>_a (tiles 0..split-1), <id>_b (the rest): each fits
            # one Arena block, or part a stays in flash (plain)
            for part, chunk in (("a", tiles_str[:split]), ("b", tiles_str[split:])):
                if plain and part == "a":
                    body = ",\n".join(" { " + t + " }" for t in chunk)
                    emit_plain(f, f"{symbol}_{part}", "uint32_t", "[32]", len(chunk), body)
                    continue
                flat = [int(v, 16) for t in chunk for v in t.split(",")]
                emit_compressed(f, f"{symbol}_{part}", "uint32_t", "[32]", len(chunk), flat)
        elif compress:
            flat = [int(v, 16) for t in tiles_str for v in t.split(",")]
            emit_compressed(f, symbol, "uint32_t", "[32]", len(tiles), flat)
        else:
            body = ",\n".join(" { " + t + " }" for t in tiles_str)
            emit_plain(f, symbol, "uint32_t", "[32]", len(tiles), body)


def _bit01_swap(a: int) -> int:
    return (a & 0xfffc) | ((a & 2) >> 1) | ((a & 1) << 1)


def _swap_if_frogger(data0: Sequence[int], data1: Sequence[int]) -> Sequence[int]:
    """In frogger the second gfx rom has D0 and D1 swapped ... wtf ...
    check first 16 bytes / two characters as they should be bitswapped
    identical in frogger."""
    if all(_bit01_swap(data0[i]) == data1[i] for i in range(16)):
        return bytes(_bit01_swap(b) for b in data0)
    return data0


def charmap_frogger(ctx: Context, symbol: str, roms: Sequence[str], header: str, *,
                    compress: bool = False) -> None:
    data0, data1 = ctx.rom(roms[0]), ctx.rom(roms[1])
    if len(data0) != 2048 or len(data1) != 2048:
        raise ConversionError(f"{symbol}: charmap ROMs are not 2048 bytes each")
    data0 = _swap_if_frogger(data0, data1)
    chars = [parse_chr_2(data0[8 * i:8 * (i + 1)], data1[8 * i:8 * (i + 1)]) for i in range(256)]
    with ctx.header(header) as f:
        _write_chars(f, symbol, chars, compress)


def unscramble_anteater(roms: Sequence[Sequence[int]]) -> list[int]:
    """Anteater's address line scramble, over the 4 KB the ROMs add up to."""
    complete = []
    for rom in roms:
        complete += rom
    out = []
    for offs in range(4096):
        src = offs & 0x9bf
        src |= (BIT(offs, 4) ^ BIT(offs, 9) ^ (BIT(offs, 2) & BIT(offs, 10))) << 6
        src |= (BIT(offs, 2) ^ BIT(offs, 10)) << 9
        src |= (BIT(offs, 0) ^ BIT(offs, 6) ^ 1) << 10
        out.append(complete[src])
    return out


def charmap_anteater(ctx: Context, symbol: str, roms: Sequence[str], header: str, *,
                     compress: bool = False) -> None:
    data = unscramble_anteater([ctx.rom(name) for name in roms])
    data0, data1 = data[0:2048], data[2048:4096]
    if len(data0) != 2048 or len(data1) != 2048:
        raise ConversionError(f"{symbol}: charmap ROMs are not 2048 bytes each")
    chars = [parse_chr_2(data0[8 * i:8 * (i + 1)], data1[8 * i:8 * (i + 1)]) for i in range(256)]
    with ctx.header(header) as f:
        _write_chars(f, symbol, chars, compress)


def charmap_bagman(ctx: Context, symbol: str, roms: Sequence[str], header: str, *,
                   compress: bool = False) -> None:
    data0, data1 = ctx.rom(roms[0]), ctx.rom(roms[1])
    data0 = _swap_if_frogger(data0, data1)
    chars = [parse_chr_2(data0[8 * i:8 * (i + 1)], data1[8 * i:8 * (i + 1)]) for i in range(1024)]
    with ctx.header(header) as f:
        _write_chars(f, symbol, chars, compress)
