"""Contra (Konami, 1987; MAME konami/contra.cpp), shown ROT90 upright.

Two K007121, each with 0x80000 bytes of gfx_8x8x4_packed_msb (16384 8x8 tiles, high nibble =
left pixel), ROM_LOAD16_BYTE interleaved. Tiles and sprites share that ROM (sprites are 8x8
blocks). Stored rotated to the panel: native pixel (x, y) of a tile -> line x, column 7 - y
(panel line = native X, column = 239 - Y). One uint32_t per panel line, column c in nibble c.

  contra_tiles     17138 unique rotated tiles of both chips, PLAIN (548 KB, read sparsely)
  contra_tile_index  2 x 16384 codes -> contra_tiles index, PLAIN (64 KB)
  contra_main_rom  633i02 + 633m03, 0x20000 banked 6809 ROM, PLAIN (over the Arena budget)
  contra_lut       4 x 256 color lookup PROMs, low nibble
  contra_sid       Gryzor.sid (HVSC, C64 port by Ocean), whole file: music and effects
The sound ROM is not used: Gryzor.sid replaces the sound CPU and YM2151.
"""
from collections.abc import Callable, Sequence
from types import MappingProxyType

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed, emit_plain
from romconv.emit.write import banner
from romconv.misc.userfile import read_user_file

_SHA1 = MappingProxyType({
    "633i02.17a": "6c29568419bc49f0be3995b0c34edd9038f6f8d9",
    "633m03.18a": "ec781e98a6efb14861223250c6239b06ec98ed0b",
    "633e04.7d": "c7d8592672a6e50c2fe6b0670001c340022f16f9",
    "633e05.7f": "a6e2598d766e6995c1a912e4a04987e6f4d547ff",
    "633e06.16d": "9ab79c06cb541ce6fdac322886b8a14a2f3f5cf7",
    "633e07.16f": "0fa0fbfc53ab5c31b9de22f90153d9af37ff22ce",
    "633e08.10g": "7c8fefdcae4523d008a7d39062194c7a80aa3500",
    "633e09.12g": "eeee2f8b3d1e4acf47de1e74c4e507ff924591e7",
    "633f10.18g": "c3bde7afb501bae58d07721c637dc06938c22150",
    "633f11.20g": "eeee2f8b3d1e4acf47de1e74c4e507ff924591e7",
})

_GFX = (("633e04.7d", "633e05.7f"), ("633e06.16d", "633e07.16f"))  # (even, odd) per K007121
_PROMS = ("633e08.10g", "633e09.12g", "633f10.18g", "633f11.20g")
_MAIN = ("633i02.17a", "633m03.18a")
_SID_FILE = "Gryzor.sid"
_SID_SHA1 = "337bfa29401763bcb0f0d0cecd8db7bb4ed94d52"
_TILE_BYTES = 32
_TILE_SIZE = 8

_Tile = Sequence[Sequence[int]]


def interleave(even: bytes, odd: bytes) -> bytes:
    """ROM_LOAD16_BYTE pair -> one region."""
    out = bytearray(2 * len(even))
    out[0::2] = even
    out[1::2] = odd
    return bytes(out)


def decode_tiles(region: bytes) -> list[list[list[int]]]:
    """gfx_8x8x4_packed_msb: 4 bytes per row, high nibble first -> tiles[t][y][x]."""
    tiles = []
    for base in range(0, len(region), _TILE_BYTES):
        rows = []
        for y in range(_TILE_SIZE):
            row = region[base + 4 * y: base + 4 * y + 4]
            rows.append([row[x // 2] >> 4 if x % 2 == 0 else row[x // 2] & 0xf for x in range(_TILE_SIZE)])
        tiles.append(rows)
    return tiles


def rotate(tile: _Tile) -> list[list[int]]:
    """Native (x, y) -> panel[line x][column 7 - y]."""
    return [[tile[_TILE_SIZE - 1 - c][line] for c in range(_TILE_SIZE)] for line in range(_TILE_SIZE)]


def pack(panel: _Tile) -> tuple[int, ...]:
    """One uint32_t per panel line, column c in nibble c."""
    return tuple(sum(p << (4 * c) for c, p in enumerate(line)) for line in panel)


def dedupe(tiles: Sequence[tuple[int, ...]]) -> tuple[list[tuple[int, ...]], list[int]]:
    unique, seen, index = [], {}, []
    for t in tiles:
        if t not in seen:
            seen[t] = len(unique)
            unique.append(t)
        index.append(seen[t])
    return unique, index


def gfx_words(read: Callable[[str], bytes]) -> list[tuple[int, ...]]:
    """Packed rotated tiles of chip 0 (codes 0-16383), then chip 1."""
    words = []
    for even, odd in _GFX:
        words += [pack(rotate(t)) for t in decode_tiles(interleave(read(even), read(odd)))]
    return words


def _body(values: Sequence[int], fmt: str, per_line: int) -> str:
    return ",\n".join(",".join(fmt % v for v in values[i:i + per_line]) for i in range(0, len(values), per_line))


def _rom(ctx: Context, name: str) -> bytes:
    return bytes(ctx.rom(name, _SHA1[name]))


def _convert_gfx(ctx: Context) -> None:
    unique, index = dedupe(gfx_words(lambda n: _rom(ctx, n)))
    rows = ",\n".join(" { " + ",".join("0x%x" % w for w in t) + " }" for t in unique)
    with ctx.header("contra_gfx.h") as f:
        f.write(banner(__file__))
        emit_plain(f, "contra_tiles", "uint32_t", "[8]", len(unique), rows)
        emit_plain(f, "contra_tile_index", "unsigned short", "", len(index), _body(index, "%d", 32))
    ctx.log.info(f"tiles: {len(unique)} unique of {len(index)}, {_TILE_BYTES * len(unique)} bytes")


def _convert_cpu(ctx: Context) -> None:
    rom = b"".join(_rom(ctx, n) for n in _MAIN)
    with ctx.header("contra_rom_main.h") as f:
        f.write(banner(__file__))
        emit_plain(f, "contra_main_rom", "unsigned char", "", len(rom), _body(rom, "0x%02x", 16))


def _convert_lut(ctx: Context) -> None:
    lut = [b & 0xf for n in _PROMS for b in _rom(ctx, n)]
    with ctx.header("contra_prom.h") as f:
        f.write(banner(__file__))
        emit_compressed(f, "contra_lut", "unsigned char", "", len(lut), lut)


def _convert_sid(ctx: Context) -> None:
    """The Jason C. Brooke tune, user supplied like the ROMs."""
    data = read_user_file(ctx, _SID_FILE, _SID_SHA1, "HVSC MUSICIANS/B/Brooke_Jason/Gryzor.sid")
    with ctx.header("contra_sid.h") as f:
        f.write(banner(__file__))
        f.write("// C64 Gryzor PSID (Jason C. Brooke, 1988 Ocean), whole file\n")
        # plain: SidPlayer copies the payload into its RAM window anyway
        emit_plain(f, "contra_sid", "unsigned char", "", len(data), _body(data, "0x%02x", 16))


def _convert(ctx: Context) -> None:
    _convert_cpu(ctx)
    _convert_gfx(ctx)
    _convert_lut(ctx)
    _convert_sid(ctx)


JOB = Job(name="contra", convert=_convert, machine="contra", zips=("contra.zip",))
