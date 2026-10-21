"""Mr. Do's Castle (Universal 1983, MAME universal/docastle.cpp, set 1).

Main Z80 ROM 32 KB, slave Z80 ROM 16 KB, 512 tiles 8x8 and 256 sprites 16x16,
both 4bpp packed msb, palette PROM 09c.bin (first 256 bytes used). CPU3 ROM
(01d.bin) is not used: the machine skips that CPU like MAME.

Gfx are stored rotated for the ROT270 panel: row r = native column W-1-r,
nibble c of each uint32 = native row c (8 pixels per uint32).
"""
from collections.abc import Sequence

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.gfx.color import rgb565_swapped

_MAIN = (("01p_a1.bin", "e397d283e8b8e1c512495b777c0fe16f66bb1862"),
         ("01n_a2.bin", "f7b0c7466cd6a3854eda818c63663e3559dc7bc2"),
         ("01l_a3.bin", "0c9e4c402f82d61d573eb55b3e2e0c8b7c8b7028"),
         ("01k_a4.bin", "7195036727990932bc94b30405ebc2b8ea5b37a8"))
_SLAVE = ("07n_a0.bin", "de71d9f142f8d420aa097d7e56b03a06db2f85fd")
_TILES = ("03a_a5.bin", "8b34773bf7b7071fb5c8ff443a08befae3b545ea")
_SPRITES = (("04m_a6.bin", "1b1f79717e5c1a23eefb2d6df71ad95aac9f8be4"),
            ("04l_a7.bin", "cc016019efe0a2bc38c66fadf792c05f6cabeeaa"),
            ("04j_a8.bin", "f951e75af658623f3b0e18ff388990b2870fad53"),
            ("04h_a9.bin", "0f06c5d248c9c92f2a4636d259ab843339737969"))
_PROM = ("09c.bin", "99f4f2d0181bcaf389c16f127cc3e632d62ee417")


def _decode(data: Sequence[int], size: int) -> list[list[int]]:
    """One size x size 4bpp packed msb element (MAME gfx_*x4_packed_msb),
    rotated: out[size-1-x][y] = pen at native (x, y)."""
    out = [[0] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            b = data[y * size // 2 + x // 2]
            out[size - 1 - x][y] = (b >> 4) if x % 2 == 0 else (b & 0x0f)
    return out


def _pack_rows(rows: Sequence[Sequence[int]]) -> list[int]:
    """Each row as uint32 words of 8 pens, column 0 in the low nibble."""
    words = []
    for row in rows:
        for i in range(0, len(row), 8):
            words.append(sum(p << (4 * c) for c, p in enumerate(row[i:i + 8])))
    return words


def _palette(prom: Sequence[int]) -> list[int]:
    """MAME docastle_palette: R bits 5-7, G bits 2-4, B bits 0-1 (no 0x23 step)."""
    def level(b0: int, b1: int, b2: int) -> int:
        return 0x23 * b0 + 0x4b * b1 + 0x91 * b2

    def bit(v: int, n: int) -> int:
        return (v >> n) & 1

    return [rgb565_swapped(level(bit(v, 5), bit(v, 6), bit(v, 7)),
                           level(bit(v, 2), bit(v, 3), bit(v, 4)),
                           level(0, bit(v, 0), bit(v, 1))) for v in prom]


def _elements(data: bytes, size: int) -> list[int]:
    step = size * size // 2
    words = []
    for i in range(0, len(data), step):
        words.extend(_pack_rows(_decode(data[i:i + step], size)))
    return words


def _convert(ctx: Context) -> None:
    main = b"".join(ctx.rom(n, s) for n, s in _MAIN)
    slave = ctx.rom(*_SLAVE)
    tiles = _elements(ctx.rom(*_TILES), 8)
    sprites = _elements(b"".join(ctx.rom(n, s) for n, s in _SPRITES), 16)
    palette = _palette(ctx.rom(*_PROM)[:256])

    with ctx.header("docastle_main_rom.h") as f:
        emit_compressed(f, "docastle_main_rom", "unsigned char", "", len(main), list(main))
    with ctx.header("docastle_slave_rom.h") as f:
        emit_compressed(f, "docastle_slave_rom", "unsigned char", "", len(slave), list(slave))
    with ctx.header("docastle_tiles.h") as f:
        emit_compressed(f, "docastle_tiles", "uint32_t", "[8]", len(tiles) // 8, tiles)
    with ctx.header("docastle_sprites.h") as f:
        emit_compressed(f, "docastle_sprites", "uint32_t", "[16][2]", len(sprites) // 32, sprites)
    with ctx.header("docastle_palette.h") as f:
        f.write("static const uint16_t docastle_palette[256] = {\n")
        for i in range(0, 256, 8):
            f.write("  " + ", ".join(f"0x{v:04x}" for v in palette[i:i + 8]) + ",\n")
        f.write("};\n")

    resident = len(main) + len(slave) + 4 * (len(tiles) + len(sprites))
    ctx.log.info(f"docastle resident compressed assets: {resident} bytes (limit 182000)")


JOB = Job(name="docastle", convert=_convert, machine="docastle", zips=("docastle.zip",), plan=True)
