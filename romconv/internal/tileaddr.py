#!/usr/bin/env python3
# Generates tileaddr.h (declaration) and tileaddr.cpp (single definition)
# into the given directory.
import os
import sys

if len(sys.argv) != 2:
    print("usage: tileaddr.py <output dir>")
    sys.exit(-1)

outdir = sys.argv[1]

map = [[None for x in range(28)] for y in range(36)]

# create mapping table from screen coordinate to memory
for offs in range(1024):
  mx = offs % 32;
  my = offs // 32;

  if my <= 1:
    sx = my + 34;
    sy = mx - 2;
  elif my >= 30:
    sx = my - 30;
    sy = mx - 2;
  else:
    sx = mx + 2;
    sy = my - 2;

  if sx>= 0 and sx < 36 and sy >=0 and sy<28:
    map[sx][27-sy] = offs

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
    vals = []
    for val in row:
      vals.append(str(val))
    rows.append(" {" + ", ".join(vals) + "}")

  print(",\n".join(rows), file=f)
  print("};", file=f)
