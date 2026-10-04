#!/usr/bin/env python3
# Menu logo converter: 224x96 PNG -> 16 color, 4 bpp Logo (see
# source/src/emulation/logo.h). Packed stream: 16 x RGB565 palette, then
# two pixels per byte, high nibble first.
import struct
import sys

sys.path.insert(0, "internal/pyconv")
from asset_emit import emit_compressed

from PIL import Image

WIDTH, HEIGHT = 224, 96
COLORS = 16

# kmeans refines the MAXCOVERAGE palette: best quality and size of the PIL
# quantizers on the current logos. No dither, it costs compression.
KMEANS_ITERATIONS = 8

def to_rgb565(r, g, b):
    """Byte swapped RGB565, as the display and the old logos expect."""
    rgb = ((r * 31 // 255) << 11) | ((g * 63 // 255) << 5) | (b * 31 // 255)
    return ((rgb & 0xff00) >> 8) | ((rgb & 0xff) << 8)

def to_rgb888(rgbs):
    """Inverse of to_rgb565. Rounds up, so to_rgb565(to_rgb888(v)) == v."""
    rgb = ((rgbs & 0xff00) >> 8) | ((rgbs & 0xff) << 8)
    r, g, b = rgb >> 11, (rgb >> 5) & 63, rgb & 31
    return ((r * 255 + 30) // 31, (g * 255 + 62) // 63, (b * 255 + 30) // 31)

def quantize(img):
    """Return (palette, indices) with palette as COLORS RGB565 values."""
    # quantize the colors the display shows, so <= 16 RGB565 colors stay exact
    shown = Image.new("RGB", img.size)
    shown.putdata([to_rgb888(to_rgb565(*p)) for p in img.get_flattened_data()])

    q = shown.quantize(colors=COLORS, method=Image.Quantize.MAXCOVERAGE,
                       kmeans=KMEANS_ITERATIONS, dither=Image.Dither.NONE)
    rgb = q.getpalette()[:3 * COLORS]
    rgb += [0] * (3 * COLORS - len(rgb))
    palette = [to_rgb565(*rgb[i:i + 3]) for i in range(0, len(rgb), 3)]
    return palette, list(q.get_flattened_data())

def write_preview(img, palette, indices, name):
    """Original (as RGB565) left, quantized right."""
    out = Image.new("RGB", (2 * WIDTH, HEIGHT))
    out.paste(Image.frombytes("RGB", img.size,
              bytes(c for p in img.get_flattened_data() for c in to_rgb888(to_rgb565(*p)))), (0, 0))
    out.paste(Image.frombytes("RGB", img.size,
              bytes(c for i in indices for c in to_rgb888(palette[i]))), (WIDTH, 0))
    out.save(name)

def parse_logo(inname, outname, preview=None):
    img = Image.open(inname).convert("RGB")
    if img.size != (WIDTH, HEIGHT):
        raise ValueError("Logo size mismatch (expected %dx%d, got %dx%d)" % (WIDTH, HEIGHT, *img.size))

    palette, indices = quantize(img)
    if preview:
        write_preview(img, palette, indices, preview)

    raw = struct.pack(f"<{COLORS}H", *palette)
    raw += bytes((indices[i] << 4) | indices[i + 1] for i in range(0, len(indices), 2))

    c_name = outname.split("/")[-1].split(".")[0]
    if c_name[0].isnumeric(): c_name = "_" + c_name

    with open(outname, "w") as f:
        print('#include "../../emulation/logo.h"', file=f)
        emit_compressed(f, c_name, "unsigned char", "", len(raw), raw)
        print(f"static constexpr Logo {c_name}({c_name}_blob);", file=f)

if len(sys.argv) not in (3, 4):
    print(f"usage: {sys.argv[0]} <logo-png> <output-header> [preview-png]")
    sys.exit(-1)

parse_logo(*sys.argv[1:])
