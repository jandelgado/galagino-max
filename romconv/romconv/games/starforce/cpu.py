from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.emit.asset import emit_compressed
from romconv.games.starforce.sets import SetSpec
from romconv.gfx.color import rgb565_swapped

_MAIN_ROM_SIZE = 0x4000
_SUB_ROM_SIZE = 0x2000


def _read_rom(ctx: Context, name: str, size: int) -> bytearray:
    data = ctx.rom(name)
    if len(data) != size:
        raise ConversionError(f"{name}: size {len(data)}, expected {size}")
    return data


def _write_rom(ctx: Context, header: str, symbol: str, rom_data: bytes) -> None:
    with ctx.header(header) as f:
        f.write(f"// File generato automaticamente per {symbol}\n")
        emit_compressed(f, symbol, "unsigned char", "", len(rom_data), list(rom_data))


def convert_cpu(ctx: Context, game: str, spec: SetSpec) -> None:
    # main CPU: two 16 KB ROMs at 0x0000 and 0x4000
    main = b"".join(_read_rom(ctx, name, _MAIN_ROM_SIZE) for name in spec.main)
    _write_rom(ctx, f"{game}_main_cpu_rom.h", f"{game}_main_cpu_rom", main)

    # sound CPU: one 8 KB ROM at 0x0000
    sub = _read_rom(ctx, spec.sub, _SUB_ROM_SIZE)
    _write_rom(ctx, f"{game}_sub_cpu_rom.h", f"{game}_sub_cpu_rom", sub)


def _palette_color(raw: int) -> int:
    """RGB565, bytes swapped, of one palette byte: intensity (2 bits) + 3 x 2 bit colors."""
    intensity = (raw >> 6) & 0x03
    b_base = (raw >> 4) & 0x03
    g_base = (raw >> 2) & 0x03
    r_base = (raw >> 0) & 0x03

    r4 = ((r_base << 2) | intensity) if r_base != 0 else 0
    g4 = ((g_base << 2) | intensity) if g_base != 0 else 0
    b4 = ((b_base << 2) | intensity) if b_base != 0 else 0

    return rgb565_swapped(r4 * 17, g4 * 17, b4 * 17)


def convert_palette(ctx: Context) -> None:
    rgb565 = [_palette_color(i) for i in range(256)]

    with ctx.header("starforce_palette.h") as f:
        f.write("// Star Force palette: RGB565\n")
        f.write("const unsigned short starforce_rgb565_palette[256] = {\n")
        for i in range(256):
            f.write("0x{:04x}".format(rgb565[i] & 0xFFFF))
            if i + 1 < len(rgb565):
                f.write(",")
                if (i + 1) % 8 == 0:
                    f.write("\n")
        f.write("};\n")
