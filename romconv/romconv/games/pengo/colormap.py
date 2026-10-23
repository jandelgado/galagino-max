from romconv.core.context import Context
from romconv.emit.asset import emit_compressed
from romconv.gfx.bits import BIT
from romconv.gfx.color import rgb565_swapped

# (pengo.zip name, name in older MAME sets, SHA-1)
_PALETTE_ROM = ("pr1633.ic78", "pr1633.78", "680eab0e1204c9b74adc11588461651b474021bb")
_LOOKUP_ROM = ("pr1634.ic88", "pr1634.88", "3fcd66610fcaee814953a115bf5e04788923181f")
_COLORS = 32
_PEN_COUNT = 4
_BANKS = 2
_GROUPS = 256
_BANK1_OFFSET = 0x10  # color index offset when the game selects palette bank 1 (I/O 0x9042)


def convert_colormap(ctx: Context) -> None:
    palette_prom_data = ctx.rom(_PALETTE_ROM[:2], _PALETTE_ROM[2])

    # the 32 base colors (MAME's resistor weights)
    base_palette = []
    for i in range(_COLORS):
        prom_byte = palette_prom_data[i]
        r = 0x21 * BIT(prom_byte, 0) + 0x47 * BIT(prom_byte, 1) + 0x97 * BIT(prom_byte, 2)
        g = 0x21 * BIT(prom_byte, 3) + 0x47 * BIT(prom_byte, 4) + 0x97 * BIT(prom_byte, 5)
        b = 0x55 * BIT(prom_byte, 6) + 0xae * BIT(prom_byte, 7)
        base_palette.append(rgb565_swapped(r, g, b))

    lookup_prom_data = ctx.rom(_LOOKUP_ROM[:2], _LOOKUP_ROM[2])

    # [palette bank][group = 2 bits of colortable bank + 6 bits color attribute][pen]
    flat = []
    for bank in range(_BANKS):
        color_offset = _BANK1_OFFSET if bank == 1 else 0x00
        for group_index in range(_GROUPS):
            lookup_base_addr = group_index * _PEN_COUNT

            for pen in range(_PEN_COUNT):
                # pen 0 is always transparent
                if pen == 0:
                    flat.append(0x0000)
                    continue

                raw_color_index = lookup_prom_data[lookup_base_addr + pen]
                flat.append(base_palette[(raw_color_index + color_offset) & 0x1F])

    with ctx.header("pengo_colormap.h") as f:
        f.write("// Colormap pre-calcolata di Pengo\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")

        f.write("// Colormap finale [palette_bank][group_index][pen] -> [2][256][4]\n")
        f.write("// Questa tabella contiene già i colori finali in RGB565 Little Endian.\n")
        emit_compressed(f, "pengo_colormap", "unsigned short", "[256][4]", _BANKS, flat)
