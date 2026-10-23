"""Helpers shared by the Namco games (Mappy, Gaplus, Tower of Druaga, Phozon, Dig Dug II, Xevious)."""
from collections.abc import Sequence

from romconv.core.context import Context
from romconv.core.header import HeaderWriter
from romconv.gfx.gfxutil import mame_decode, rot_galagino
from romconv.hw.namco import nudge

# charlayout pacman/galaga/mappy: 8x8x2bpp, 16 bytes per char
CHAR_PLANES = (0, 4)
CHAR_XOFFS = (64, 65, 66, 67, 0, 1, 2, 3)
_CHAR_YOFFS = tuple(y * 8 for y in range(8))

# mappy spritelayout_4bpp: 16x16x4bpp, 128 bytes per sprite (16 bit interleaved stream)
_SPRITE_PLANES = (0, 4, 8, 12)
_SPRITE_XOFFS = (0, 1, 2, 3, 128, 129, 130, 131, 256, 257, 258, 259, 384, 385, 386, 387)
_SPRITE_YOFFS = tuple(y * 16 for y in range(8)) + tuple(512 + y * 16 for y in range(8))

_NO_COLOR = 0x0F  # lookup nibble of hardware transparency
_PRIO_GROUPS = 64


def tile_rows(tiles: Sequence[Sequence[Sequence[int]]]) -> list[int]:
    """One unsigned short per tile row, pixel 0 in bits 0-1."""
    flat = []
    for t in tiles:
        for y in range(8):
            v = 0
            for x in range(8):
                v |= t[y][x] << (2 * x)
            flat.append(v)
    return flat


def sprite_words(sprites: Sequence[Sequence[Sequence[int]]]) -> list[int]:
    """Two uint32_t per sprite row of 16 4bpp pixels, pixel 0 in the low nibble."""
    flat = []
    for t in sprites:
        for y in range(16):
            v = 0
            for x in range(16):
                v |= t[y][x] << (4 * x)
            flat.append(v & 0xffffffff)
            flat.append(v >> 32)
    return flat


def inverted_tiles(rom: Sequence[int]) -> list[list[list[int]]]:
    """256 galagino-rotated chars of a ROMREGION_INVERT tile ROM."""
    data = bytes(b ^ 0xFF for b in rom)
    return [rot_galagino(t) for t in
            mame_decode(data, 8, 8, CHAR_PLANES, CHAR_XOFFS, _CHAR_YOFFS, 128, 256)]


def interleaved_sprites(even: Sequence[int], odd: Sequence[int], size: int,
                        count: int) -> list[list[list[int]]]:
    """Galagino-rotated 4bpp sprites of a 16 bit interleaved stream (even/odd bytes)."""
    data = bytearray(size)
    data[0::2] = even
    data[1::2] = odd
    return [rot_galagino(s) for s in
            mame_decode(bytes(data), 16, 16, _SPRITE_PLANES, _SPRITE_XOFFS, _SPRITE_YOFFS, 1024, count)]


def write_wsg(ctx: Context, header: str, symbol: str, comment: str, prom: Sequence[int]) -> None:
    """WSG 15XX waveforms: 8 waves x 32 samples, low nibble centered (-8..7)."""
    with ctx.header(header) as f:
        print(f"// {comment}", file=f)
        print(f"const signed char {symbol}[][32] = {{", file=f)
        rows = []
        for w in range(8):
            vals = [str((prom[w * 32 + i] & 0x0f) - 8) for i in range(32)]
            rows.append(" { " + ",".join(vals) + " }")
        print(",\n".join(rows), file=f)
        print("};", file=f)


def tile_tables(f: HeaderWriter, name: str, pal: Sequence[int], char_lut: Sequence[int]) -> None:
    """<name>_colormap_tiles (opaque) and _tiles_prio (0 where the lookup is transparent):
    64 groups x 4 pens, chars use palette entries 16-31."""
    print(f"const unsigned short {name}_colormap_tiles[][4] = {{", file=f)
    rows = []
    for g in range(_PRIO_GROUPS):
        vals = [hex(nudge(pal[16 + (char_lut[g * 4 + p] & 0x0f)])) for p in range(4)]
        rows.append("{" + ",".join(vals) + "}")
    print(",\n".join(rows), file=f)
    print("};", file=f)
    print(f"const unsigned short {name}_colormap_tiles_prio[][4] = {{", file=f)
    rows = []
    for g in range(_PRIO_GROUPS):
        vals = []
        for p in range(4):
            lut = char_lut[g * 4 + p] & 0x0f
            vals.append(hex(0) if lut == _NO_COLOR else hex(nudge(pal[16 + lut])))
        rows.append("{" + ",".join(vals) + "}")
    print(",\n".join(rows), file=f)
    print("};", file=f)
