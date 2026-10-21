from collections.abc import Sequence

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.header import HeaderWriter
from romconv.emit.asset import emit_compressed
from romconv.gfx.color import rgb565_swapped_4bit, rgb565_swapped_dkong, rgb565_swapped_packed


def palette(ctx: Context, rom: str | Sequence[str], rom2: str | None = None) -> list[int]:
    """RGB565 palette from the palette PROM(s).

    rom: one PROM of bbgggrrr bytes (galaga, pacman, frogger), or the red,
    green and blue PROMs (4 bit each). With rom2 (the high nibbles): dkong's
    rrrgggbb mapping.

    galaga: the palette contains 32 8 bit rgb values. The first 16 are
    used by sprites and the second 16 by tiles."""
    if not isinstance(rom, str):
        r, g, b = (ctx.rom(name) for name in rom)
        # the proms contain 4 bit data
        return [rgb565_swapped_4bit(r[i], g[i], b[i]) for i in range(256)]

    data = ctx.rom(rom)
    data2 = ctx.rom(rom2) if rom2 else None
    if data2:
        # dkong uses two 4 bit color proms, so there's a second one
        return [rgb565_swapped_dkong((data2[i] << 4) + c) for i, c in enumerate(data)]
    # This doesn't 100% match the weighting each bit has
    # on the real machine with 1000, 470 and 220 ohms resistors.
    #
    # Here the weighting is 4/7, 2/7 and 1/7 while on the
    # real thing it's 4.15/7, 1.94/7 and 0.91/7. So the
    # weighting of the lsb is slightly higher on the real
    # device.
    return [rgb565_swapped_packed(c) for c in data]


def _check_indices(c: Sequence[int]) -> None:
    # values must be 4 bit palette indices
    if any(v > 15 for v in c):
        raise ConversionError("Color index out of range")


def colormap_1942_tiles(ctx: Context, symbol: str, roms: Sequence[str], palette: Sequence[int],
                        header: str) -> None:
    """1942 tile colormaps: roms = (colormap PROM, map PROM 0, map PROM 1)."""
    colormap_data = ctx.rom(roms[0])
    map0, map1 = ctx.rom(roms[1]), ctx.rom(roms[2])

    # only use lower 2 bits of sb-2.d1
    index_map = [map0[i] + 16 * (map1[i] & 3) for i in range(len(map0))]

    # output of colormap_data is 4 bit and goes into lower four
    # bits of sb-3/sb-2 map. The upper 4 bits come externally,
    # resulting in 16 tables with 32 x 8 color maps each
    flat = []
    for tab in range(16):
        for idx in range(32):
            c = colormap_data[8 * idx:8 * (idx + 1)]
            _check_indices(c[:4])
            for a in c:
                flat.append(palette[index_map[16 * tab + a]])

    with ctx.header(header) as f:
        emit_compressed(f, symbol, "unsigned short", "[32][8]", 16, flat)


def _hex_row(values: Sequence[int]) -> str:
    return "{" + ",".join(hex(v) for v in values) + "}"


def colormap(ctx: Context, symbol: str, rom: str, palette: Sequence[int], header: str, *,
             compress: bool = False) -> None:
    """64 colormaps of 4 palette entries, from a 256 byte PROM."""
    colormap_data = ctx.rom(rom)

    # the colormaps contain 64*4 4-bit entries that point
    # to palette entries.
    if len(colormap_data) != 256:
        raise ConversionError(f"{rom}: missing colormap data")

    flat = []
    for idx in range(64):
        c = colormap_data[4 * idx:4 * (idx + 1)]
        _check_indices(c)
        for a in c:
            flat.append(palette[a])

    with ctx.header(header) as f:
        if compress:
            emit_compressed(f, symbol, "unsigned short", "[4]", 64, flat)
        else:
            print("const unsigned short " + symbol + "[][4] = {", file=f)
            print(",\n".join(_hex_row(flat[4 * idx:4 * idx + 4]) for idx in range(64)), file=f)
            print("};", file=f)


def _dkong_screens(rows: Sequence[Sequence[int]], palette: Sequence[int]) -> str:
    """4 screens of palette rows; each row indexes 4 palette entries."""
    screens = []
    for s in range(4):
        colors = [_hex_row([palette[a] for a in (4 * (idx + 16 * s) + k for k in range(4))]) for idx in rows[s]]
        screens.append("{" + ",\n".join(colors) + "}")
    return ",\n".join(screens)


def colormap_dkong(ctx: Context, symbol: str, rom: str, palette: Sequence[int], header: str, *,
                   compress: bool = False) -> None:
    """Donkey Kong tile colormaps (4 screens) and the sprite colormaps."""
    colormap_data = ctx.rom(rom)
    if len(colormap_data) != 256:
        raise ConversionError(f"{rom}: missing colormap data")

    with ctx.header(header) as f:
        if compress:
            flat = []
            for s in range(4):
                for idx in colormap_data:
                    idx += 16 * s
                    for a in [4 * idx + 0, 4 * idx + 1, 4 * idx + 2, 4 * idx + 3]:
                        flat.append(palette[a])
            emit_compressed(f, symbol, "unsigned short", "[256][4]", 4, flat)
        else:
            print("const unsigned short " + symbol + "[][256][4] = {", file=f)
            print(_dkong_screens([colormap_data] * 4, palette), file=f)
            print("};", file=f)

        print("", file=f)
        print("", file=f)

        # create the four sprite colormap tables (always plain: 512 bytes total,
        # too small for zlib to net a win)
        print("const unsigned short " + symbol + "_sprite[][16][4] = {", file=f)
        print(_dkong_screens([range(16)] * 4, palette), file=f)
        print("};", file=f)


def dump_palette(ctx: Context, symbol: str, header: str, palette: Sequence[int]) -> None:
    """The first 32 palette entries as 8 rows of 4."""
    with ctx.header(header) as f:
        print("const unsigned short " + symbol + "[][4] = {", file=f)
        print(",\n".join(_hex_row(palette[4 * idx:4 * (idx + 1)]) for idx in range(8)), file=f)
        print("};", file=f)
