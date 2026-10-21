#!/usr/bin/env python3
"""Generates source/src/machines/tileaddr.h (declaration) and tileaddr.cpp (single definition).

usage (romconv/): uv run python -m dev.tileaddr [OUTPUT DIR]   (default: the tracked location)
"""
import os
import sys

from romconv.core.paths import default

ROWS, COLS = 36, 28


def tile_map():
    """Screen coordinate -> video RAM offset."""
    map = [[None for x in range(COLS)] for y in range(ROWS)]
    for offs in range(1024):
        mx = offs % 32
        my = offs // 32

        if my <= 1:
            sx = my + 34
            sy = mx - 2
        elif my >= 30:
            sx = my - 30
            sy = mx - 2
        else:
            sx = mx + 2
            sy = my - 2

        if sx >= 0 and sx < ROWS and sy >= 0 and sy < COLS:
            map[sx][27 - sy] = offs
    return map


def main(argv):
    if len(argv) > 2:
        print("usage: python -m dev.tileaddr [output dir]")
        return 1
    outdir = argv[1] if len(argv) == 2 else default().machines
    map = tile_map()

    with open(os.path.join(outdir, "tileaddr.h"), "w") as f:
        print("#ifndef _TILEADDR_H_", file=f)
        print("#define _TILEADDR_H_", file=f)
        print("", file=f)
        print("extern const unsigned short tileaddr[36][28];", file=f)
        print("", file=f)
        print("#endif", file=f)

    with open(os.path.join(outdir, "tileaddr.cpp"), "w") as f:
        print('#include "tileaddr.h"', file=f)
        print("", file=f)
        print("const unsigned short tileaddr[36][28] = {", file=f)
        rows = []
        for row in map:
            rows.append(" {" + ", ".join(str(val) for val in row) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
