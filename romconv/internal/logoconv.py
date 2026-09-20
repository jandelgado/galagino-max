#!/usr/bin/env python3
import sys

sys.path.insert(0, "internal/pyconv")
from romdata_emit import emit_compressed

from PIL import Image

def parse_logo(inname, outname):
    img = Image.open(inname).convert("RGB")

    # expect a 224x96 logo
    if img.size != (224, 96):
        raise ValueError("Logo size mismatch (expected 224x96, got %dx%d)" % img.size)

    px = img.load()
    colset = set()
    # convert all pixels to 16 bit 565 rgb
    rgb565 = []
    for y in range(96):
        for x in range(224):
            r, g, b = px[x, y]
            rgb = ((r * 31 // 255) << 11) | ((g * 63 // 255) << 5) | (b * 31 // 255)
            rgbs = ((rgb & 0xff00) >> 8) + ((rgb & 0xff) << 8)
            colset.add(rgbs)
            rgb565.append(rgbs)

    print("Colors:", len(colset))

    c_name = outname.split("/")[-1].split(".")[0]
    if c_name[0].isnumeric(): c_name = "_" + c_name

    with open(outname, "w") as f:
        emit_compressed(f, c_name, "unsigned short", "", len(rgb565), rgb565)

if len(sys.argv) != 3:
    print(f"usage: {sys.argv[0]} <logo-png> <output-header>")
    sys.exit(-1)

parse_logo(sys.argv[1], sys.argv[2])
