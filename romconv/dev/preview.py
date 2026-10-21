"""Shared helpers of the preview tools. Images go to build/preview/<game>/ only."""
import logging
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from romconv import registry
from romconv.core.context import Context
from romconv.core.manifest import Asset
from romconv.core.paths import Paths, default

PALETTE = {
    0: (0, 0, 0), 1: (255, 85, 85), 2: (85, 255, 85), 3: (255, 255, 85),
    4: (85, 85, 255), 5: (255, 85, 255), 6: (85, 255, 255), 7: (255, 255, 255),
}
_DTYPES = {"unsigned char": "u1", "signed char": "i1", "unsigned short": "<u2", "uint32_t": "<u4"}


def preview_dir(game: str) -> Path:
    out = default().build / "preview" / game
    out.mkdir(parents=True, exist_ok=True)
    return out


def context(job_name: str) -> Context:
    """Context of a job on a scratch repo under build/preview/<job>/scratch (the real tree
    stays untouched); ROMs come from the real romszip/."""
    real = default()
    job = registry.by_name(job_name)
    scratch = preview_dir(job_name) / "scratch"
    scratch.mkdir(exist_ok=True)
    for name in ("romszip", "samples"):
        link = scratch / name
        if not link.exists() and (real.repo / name).is_dir():
            link.symlink_to(real.repo / name)
    return Context(job, Paths(scratch), (), logging.Logger(job_name))


def convert(job_name: str) -> Context:
    """Run the job on its scratch context: ctx.manifest holds the assets, ctx.paths the headers."""
    ctx = context(job_name)
    ctx.job.convert(ctx)
    return ctx


def values(asset: Asset) -> list[int]:
    """The asset's elements, flat, as the C initializer lists them."""
    return np.frombuffer(asset.raw, dtype=_DTYPES[asset.ctype]).tolist()


def asset(ctx: Context, name: str) -> Asset:
    (found,) = [a for a in ctx.manifest.assets() if a.name == name]
    return found


def grid(tiles, tile_w, tile_h, palette, tiles_per_row, output_file, bg=(30, 30, 30)):
    """Labeled grid PNG of decoded tiles (tile[y][x] = pen)."""
    rows = (len(tiles) + tiles_per_row - 1) // tiles_per_row
    cell_w, cell_h = tile_w + 4, tile_h + 14
    img = Image.new("RGB", (cell_w * tiles_per_row, cell_h * rows), bg)
    draw = ImageDraw.Draw(img)
    font = ImageFont.load_default()
    for idx, tile in enumerate(tiles):
        row, col = divmod(idx, tiles_per_row)
        x0, y0 = col * cell_w + 2, row * cell_h + 2
        for y in range(tile_h):
            for x in range(tile_w):
                img.putpixel((x0 + x, y0 + y), palette.get(tile[y][x], bg))
        draw.text((x0, y0 + tile_h), str(idx), font=font, fill=(200, 200, 200))
        draw.rectangle([x0 - 1, y0 - 1, x0 + tile_w, y0 + tile_h], outline=(80, 80, 80))
    img.save(output_file)
    print(f"Preview saved as '{output_file}' ({len(tiles)} tiles)")
