#!/usr/bin/env python3
"""MAME taitosj screen_update (elevatorb) in Python, from mame_fixture.lua dumps.

Usage: uv run python -m dev.elevator.ref_render <dump dir> <frame>:<snapshot png> [out dir]
Prints differing pixels against the snapshot, writes <frame>_ref.png.
"""
import logging
import os
import sys

from PIL import Image

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.core.paths import default

LEVEL = [0, 37, 79, 117, 138, 176, 218, 255]
FUDGE1, FUDGE2 = (3, 1, -1), (8, 10, 12)


def parse(path):
    cur, prev, charram = open(path).read().split()
    b = lambda h: bytes.fromhex(h)
    return b(cur), b(prev), b(charram)


def fields(blob):
    o, f = 0, {}
    for name, n in (("vram", 0xc00), ("colscroll", 0x60), ("spr", 0x80), ("pal", 0x80),
                    ("scroll", 6), ("colorbank", 2), ("mode", 1), ("prio", 1)):
        f[name] = blob[o:o + n]
        o += n
    return f


def palette(pal):
    out = []
    for i in range(64):
        lo, hi = pal[2 * i], pal[2 * i + 1]
        r = LEVEL[((~hi >> 6) & 1) | ((~hi >> 6) & 2) | ((~lo << 2) & 4)]
        g = LEVEL[(~hi >> 3) & 7]
        b = LEVEL[~hi & 7]
        out.append((r, g, b))
    return out


def char_pixel(cr, bank, code, x, y):
    base = bank * 0x1800 + code * 8 + y
    return (((cr[base + 0x1000] >> x) & 1) << 2) | (((cr[base + 0x800] >> x) & 1) << 1) | ((cr[base] >> x) & 1)


def sprite_pixel(cr, bank, code, x, y):
    quad = (x >> 3) + 2 * (y >> 3)
    return char_pixel(cr, bank, code * 4 + quad, x & 7, y & 7)


def render(f, spr_f, cr, order):
    pens = palette(f["pal"])
    cb = f["colorbank"]
    mode = f["mode"][0]
    screen = [[8 * (cb[1] & 7)] * 256 for _ in range(256)]

    def layer(w):
        if not mode & (0x10 << w):
            return
        bank = (cb[0] & 0x08, cb[0] & 0x80, cb[1] & 0x08)[w] != 0
        color = (cb[0] & 7, (cb[0] >> 4) & 7, cb[1] & 7)[w]
        sx = f["scroll"][2 * w]
        scrollx = -(sx & 0xf8) + ((sx + FUDGE1[w]) & 7) + FUDGE2[w]
        scrolly = [-f["colscroll"][32 * w + i] - f["scroll"][2 * w + 1] for i in range(32)]
        vram = f["vram"][0x400 * w:0x400 * (w + 1)]
        for y in range(256):
            for x in range(256):
                srcx = (x - scrollx) & 255
                col = srcx >> 3
                srcy = (y - scrolly[col]) & 255
                code = vram[(srcy >> 3) * 32 + (srcx >> 3)]
                p = char_pixel(cr, bank, code, srcx & 7, srcy & 7)
                if p:
                    screen[y][x] = color * 8 + p

    def sprites():
        if not mode & 0x80:
            return
        s = spr_f["spr"]
        for sprite in range(0x1f, -1, -1):
            which = (sprite - 1) & 0x1f
            if 0x10 <= which <= 0x17:
                continue
            o = which * 4
            sx = (s[o] - 1) & 0xff
            sy = (240 - s[o + 1]) & 0xff
            if sy >= 240:
                continue
            code = s[o + 3] & 0x3f
            bank = 1 if s[o + 3] & 0x40 else 0
            color = 2 * ((cb[1] >> 4) & 3) + ((s[o + 2] >> 2) & 1)
            fx, fy = s[o + 2] & 1, s[o + 2] & 2
            for px0 in (sx, sx - 256):
                for yy in range(16):
                    for xx in range(16):
                        X, Y = px0 + xx, sy + yy
                        if not (3 <= X <= 254 and 16 <= Y <= 239):
                            continue
                        p = sprite_pixel(cr, bank, code, 15 - xx if fx else xx, 15 - yy if fy else yy)
                        if p:
                            screen[Y][X] = color * 8 + p

    for plane in order[f["prio"][0] & 0x1f]:
        if plane == 0:
            sprites()
        else:
            layer(plane - 1)
    return [[pens[screen[y][x]] for x in range(256)] for y in range(16, 240)]


# MAME compute_draw_order(), as romconv/games/elevator.py
def draw_order():
    ctx = Context(Job("elevator", lambda c: None, zips=("elevator.zip",)), default(), (), logging.Logger("elevator"))
    prom = ctx.rom("eb16.ic22", "d233f1bf8a3e6cd876853ffd721b9b64c61c9047")
    order = []
    for i in range(32):
        mask, planes = 0, [0] * 4
        for j in range(3, -1, -1):
            data = prom[0x10 * (i & 0x0f) + mask] & 0x0f
            data = data >> 2 if i & 0x10 else data & 0x03
            mask |= 1 << data
            planes[j] = data
        order.append(planes)
    return order


def main():
    d, pick = sys.argv[1], sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else d
    frame, png = pick.split(":")
    cur, prev, cr = parse(os.path.join(d, f"{frame}.txt"))
    for name, spr in (("cur", cur), ("prev", prev)):
        img = render(fields(cur), fields(spr), cr, draw_order())
        snap = Image.open(png).convert("RGB")
        diff = sum(img[y][x] != snap.getpixel((x, y)) for y in range(224) for x in range(256))
        print(f"frame {frame} sprites {name}: {diff} differing pixels")
        im = Image.new("RGB", (256, 224))
        im.putdata([p for row in img for p in row])
        im.save(os.path.join(out, f"{frame}_ref_{name}.png"))


if __name__ == "__main__":
    main()
