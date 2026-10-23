from collections.abc import Sequence

import numpy as np

from romconv.core.context import Context
from romconv.gfx.color import rgb565_swapped

_PALETTE_ROMS = ("u02--2.bin", "t02--3.bin")
_LOOKUP_ROM = "f10--1.bin"
_LOOKUP_SIZE = 32
_PENS = 4
_TRANSPARENT = 0x0000


def _weights() -> list[int]:
    """MAME mrdo_state::palette_init(): 0..255 level of the 4 diode-coupled
    resistors (75, 100 ohm on U2, 120, 150 on T2) into 220 ohm, in float."""
    f = np.float32
    weight = [0] * 16
    pot = [f(0)] * 16
    for i in range(15, -1, -1):
        par = f(0)
        for bit, ohm in enumerate((150, 120, 100, 75)):
            if i >> bit & 1:
                par += f(1) / f(ohm)
        if par:
            pot[i] = f(220) / (f(220) + f(1) / par) - f(0.7)
        weight[i] = max(0, int(f(255) * pot[i] / pot[15]))
    return weight


def _master_rgb(u2: bytes, t2: bytes) -> list[tuple[int, int, int]]:
    """256 (r, g, b): T2 gives bits 0-1, U2 bits 2-3 of each channel's weight index."""
    weight = _weights()
    rgb = []
    for i in range(256):
        a1 = (i >> 3 & 0x1c) + (i & 3)
        a2 = (i & 0x1c) + (i & 3)
        rgb.append(tuple(weight[(t2[a1] >> s & 3) + ((u2[a2] >> s & 3) << 2)] for s in (0, 2, 4)))
    return rgb


def _master_palette(ctx: Context) -> list[int]:
    return [rgb565_swapped(*c) for c in _master_rgb(*(ctx.rom(n) for n in _PALETTE_ROMS))]


def convert_palette(ctx: Context) -> None:
    rgb = _master_rgb(*(ctx.rom(n) for n in _PALETTE_ROMS))
    with ctx.header("mrdo_palette.h") as f:
        f.write("// Mr. Do! master palette, RGB565 byte-swapped (MAME mrdo palette_init).\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("const uint16_t mrdo_master_palette[256] = {\n")

        for i, (r, g, b) in enumerate(rgb):
            f.write(f"  0x{rgb565_swapped(r, g, b):04X}, // {i:3d}: ({r:3d}, {g:3d}, {b:3d})\n")

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
    master_palette = _master_palette(ctx)
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
