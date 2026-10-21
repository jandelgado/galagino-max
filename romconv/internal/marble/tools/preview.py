#!/usr/bin/env python3
"""Marble Madness previews: full levels and 240x288 viewport frames (x pan 0/24/48).

usage (romconv/): uv run python internal/marble/tools/preview.py OUTDIR [LEVEL[:Y] ...]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw
from devrom import ROM, md

W, H, PAN = 240, 288, 48


def render(rom, n):
    m = md.decode_level(rom, n)
    pal = md.palette(rom, n)
    cache = {}
    img = Image.new("RGB", (md.COLS * 8, len(m) * 8))
    px = img.load()
    for y, row in enumerate(m):
        for x, w in enumerate(row):
            code, cs, flip = md.pf_lookup(rom, w)
            if code not in cache:
                cache[code] = md.pf_tile(rom, code)
            t = cache[code]
            for ty in range(8):
                for tx in range(8):
                    c = t[ty * 8 + (7 - tx if flip else tx)]
                    px[x * 8 + tx, y * 8 + ty] = pal[md.PF_PALETTE + cs * 32 + c]
    return img


def frames(full, y, name):
    out = Image.new("RGB", (3 * W + 4 * 12, H + 30), (40, 40, 40))
    d = ImageDraw.Draw(out)
    for k, sx in enumerate((0, PAN // 2, PAN)):
        out.paste(full.crop((sx, y, sx + W, y + H)), (12 + k * (W + 12), 24))
        d.text((12 + k * (W + 12), 6), f"{name} y={y} pan={sx}", fill=(255, 255, 255))
    return out.resize((out.width * 2, out.height * 2), Image.NEAREST)


def main():
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    rom = ROM
    full = {n: render(rom, n) for n in range(md.LEVELS)}
    for n, img in full.items():
        img.save(os.path.join(outdir, f"level{n}_{md.NAMES[n]}.png"))
    args = sys.argv[2:] or [f"{n}:{y}" for n in range(md.LEVELS)
                            for y in (0, (full[n].height - H) // 2, full[n].height - H)]
    for a in args:
        n, _, y = a.partition(":")
        n, y = int(n), int(y or 0)
        path = os.path.join(outdir, f"view_{n}_{md.NAMES[n]}_y{y:04d}.png")
        frames(full[n], y, md.NAMES[n]).save(path)
        print(path)


if __name__ == "__main__":
    main()
