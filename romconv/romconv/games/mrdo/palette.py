from collections.abc import Sequence

from romconv.core.context import Context
from romconv.games.mrdo.palette_data import MASTER_PALETTE_RGB888
from romconv.gfx.color import rgb565_be, rgb565_swapped

_LOOKUP_ROM = "f10--1.bin"
_LOOKUP_SIZE = 32
_PENS = 4
_TRANSPARENT = 0x0000


def _master_palette() -> list[int]:
    return [rgb565_swapped(r, g, b) for r, g, b in MASTER_PALETTE_RGB888]


def convert_palette(ctx: Context) -> None:
    with ctx.header("mrdo_palette.h") as f:
        f.write("// Master Palette di Mr. Do! convertita in formato RGB565 LITTLE ENDIAN.\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("// Palette completa da 256 colori (formato Little Endian)\n")
        f.write("const uint16_t mrdo_master_palette[256] = {\n")

        for i, ((r, g, b), le_color) in enumerate(zip(MASTER_PALETTE_RGB888, _master_palette())):
            f.write(f"  0x{le_color:04X},")
            f.write(f" // Indice {i:3d} | Original: ({r:4d}, {g:4d}, {b:4d}) | BE: 0x{rgb565_be(r, g, b):04X}\n")

        f.write("};\n")


def _sprite_group(master_palette: Sequence[int], lookup_prom: Sequence[int], palette_num: int) -> list[int]:
    group_colors = [_TRANSPARENT] * _PENS

    for pen_num in range(1, _PENS):
        mame_loop_i = (palette_num * _PENS) + pen_num
        ctabentry_base = lookup_prom[mame_loop_i & 0x1f]
        ctabentry = (ctabentry_base >> 4) if (mame_loop_i & 0x20) else (ctabentry_base & 0x0f)
        group_colors[pen_num] = master_palette[ctabentry + ((ctabentry & 0x0c) << 3)]

    return group_colors


def convert_sprite_colormap(ctx: Context) -> None:
    master_palette = _master_palette()
    lookup_prom = ctx.rom(_LOOKUP_ROM)[:_LOOKUP_SIZE]

    with ctx.header("mrdo_sprite_colormap.h") as f:
        f.write("// Colormap per gli SPRITE di Mr. Do! (formato Little Endian).\n")
        f.write("// Generato usando 'source/src/machines/mrdo/mrdo_palette.h' "
                "e 'mrdo.zip: f10--1.bin'.\n\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("const uint16_t mrdo_sprite_colormap[16][4] = {\n")

        for palette_num in range(16):
            group_colors = _sprite_group(master_palette, lookup_prom, palette_num)
            f.write(f"  {{ {', '.join(f'0x{c:04X}' for c in group_colors)} }}")
            if palette_num < 15:
                f.write(",")
            f.write(f" // Palette {palette_num}\n")

        f.write("};\n")
