# Menu logo converter: 224x96 PNG -> 16 color, 4 bpp Logo (see
# source/src/emulation/logo.h). Packed stream: 16 x RGB565 palette, then
# two pixels per byte, high nibble first.
import struct
from pathlib import Path
from types import MappingProxyType

from PIL import Image

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.emit.text import c_symbol
from romconv.gfx.color import rgb565_swapped_scaled, swap_bytes

WIDTH, HEIGHT = 224, 96
_COLORS = 16

# kmeans refines the MAXCOVERAGE palette: best quality and size of the PIL
# quantizers on the current logos. No dither, it costs compression.
_KMEANS_ITERATIONS = 8

# PNG names that differ from the machine dir
_MACHINE = MappingProxyType({"invaders": "spaceinvaders", "milliped": "millipede"})


def _to_rgb888(rgbs: int) -> tuple[int, int, int]:
    """Inverse of rgb565_swapped_scaled. Rounds up, so the round trip is exact."""
    rgb = swap_bytes(rgbs)
    r, g, b = rgb >> 11, (rgb >> 5) & 63, rgb & 31
    return ((r * 255 + 30) // 31, (g * 255 + 62) // 63, (b * 255 + 30) // 31)


def _quantize(img: Image.Image) -> tuple[list[int], list[int]]:
    """Return (palette, indices) with palette as _COLORS RGB565 values."""
    # quantize the colors the display shows, so <= 16 RGB565 colors stay exact
    shown = Image.new("RGB", img.size)
    shown.putdata([_to_rgb888(rgb565_swapped_scaled(*p)) for p in img.get_flattened_data()])

    q = shown.quantize(colors=_COLORS, method=Image.Quantize.MAXCOVERAGE,
                       kmeans=_KMEANS_ITERATIONS, dither=Image.Dither.NONE)
    rgb = q.getpalette()[:3 * _COLORS]
    rgb += [0] * (3 * _COLORS - len(rgb))
    palette = [rgb565_swapped_scaled(*rgb[i:i + 3]) for i in range(0, len(rgb), 3)]
    return palette, list(q.get_flattened_data())


def logo(ctx: Context, png: Path, machine: str) -> None:
    """Convert one PNG to machines/<machine>/<machine>_logo.h."""
    img = Image.open(png).convert("RGB")
    if img.size != (WIDTH, HEIGHT):
        raise ConversionError("%s: logo size mismatch (expected %dx%d, got %dx%d)"
                              % (png.name, WIDTH, HEIGHT, *img.size))

    palette, indices = _quantize(img)
    raw = struct.pack(f"<{_COLORS}H", *palette)
    raw += bytes((indices[i] << 4) | indices[i + 1] for i in range(0, len(indices), 2))

    c_name = c_symbol(machine + "_logo")
    with ctx.header(f"{machine}_logo.h", machine=machine, plan=False, remote=False) as f:
        print('#include "../../emulation/logo.h"', file=f)
        emit_compressed(f, c_name, "unsigned char", "", len(raw), raw, remote=False)
        print(f"static constexpr Logo {c_name}({c_name}_blob);", file=f)


def _convert(ctx: Context) -> None:
    for png in sorted(ctx.paths.logos.glob("*.png"), key=lambda p: p.name):
        machine = _MACHINE.get(png.stem, png.stem)
        if (ctx.paths.machines / machine).is_dir():
            logo(ctx, png, machine)


LOGOS_JOB = Job(name="logos", convert=_convert, always=True)
