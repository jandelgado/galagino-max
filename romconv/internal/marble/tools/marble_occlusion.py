#!/usr/bin/env python3
"""Marble Madness occlusion prototype: render a MAME frame from its RAM and compare.

A level object never hides the marble: the player's occlusion masks do (0x264aa, ported in
marble_sim.py `occlusion_264aa`, MARBLE.md "Occlusion"). They are priority motion objects
drawn after the marble; MAME atarisy1_v.cpp: a priority MO pixel with pen 1 keeps the
playfield pixel, so a mask erases the motion objects drawn before it (all mask tiles use
pen 1 only).

usage: marble_occlusion.py SHOTS.BIN SNAPDIR TRACE.BIN RACE OUTDIR [N ...]
  SHOTS.BIN, SNAPDIR: tools/mame_trace.lua with MARBLE_SHOTS (record n = SNAPDIR/n.png).
  Writes OUTDIR/occl_N.png: MAME | MO RAM render | remake rule | remake without masks,
  and prints the pixel differences against the MAME frame (exit 1 when a render that
  should be exact differs).

Renders:
  mo:     playfield RAM + motion object RAM (the bank the hardware shows), MAME merge.
  remake: playfield RAM + the player's 7 MOs from MO RAM + masks from the logic entries
          (object +0x38, x + 24, y + 16, code bits: 15 hflip, 11-13 height - 1, 0-10 MO
          code), rule: a mask pixel (pen 1) clears the marble pixel. The entries come from
          the logic tick (TRACE.BIN) whose masks the shown MO bank holds (the MO list is
          built one tick after the logic, together with the marble MOs).
  nomask: same without masks (the remake before Phase 3).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from devrom import ROM, md
import marble_sim as ms

W, H = 336, 240
P = ms.P
MO_X, MO_Y = 24, 16          # mask entry (x, y) -> MO (x, y)
PRIO = 0x8000


class Frame:
    def __init__(self, rom, race, ram, vram):
        self.rom, self.ram, self.vram = rom, ram, vram
        self.pal = md.palette(rom, race)
        self.tiles = {}

    def w(self, buf, off): return struct.unpack(">H", buf[off:off + 2])[0]

    def pf_tile(self, code):
        if ("pf", code) not in self.tiles:
            self.tiles[("pf", code)] = md.pf_tile(self.rom, code)
        return self.tiles[("pf", code)]

    def mo_tile(self, code):
        if ("mo", code) not in self.tiles:
            tile, cs = md.mo_lookup(self.rom, code)
            self.tiles[("mo", code)] = (md.mo_tile(self.rom, tile), cs)
        return self.tiles[("mo", code)]

    def playfield(self, yoff):
        """Palette index per screen pixel: screen x X = PF column X / 8 (x scroll 0), line L =
        PF line (yscroll + L + yoff) & 0x1ff, yscroll = work RAM 0x400000."""
        ys = self.w(self.ram, 0)
        pf = [[0] * W for _ in range(H)]
        for L in range(H):
            line = (ys + L + yoff) & 0x1ff
            for X in range(W):
                word = self.w(self.vram, ((line >> 3) * 64 + (X >> 3)) * 2)
                code, cs, flip = md.pf_lookup(self.rom, word)
                tx = X & 7
                pf[L][X] = md.PF_PALETTE + cs * 32 + self.pf_tile(code)[(line & 7) * 8 + (7 - tx if flip else tx)]
        return pf

    def mo_list(self, bank):
        """(word0, code, word2) in link order (MAME atarimo: start link 0, max 0x38)."""
        base = 0x2000 + bank * 0x200
        out, link, seen = [], 0, set()
        while link not in seen and len(out) < 0x38:
            seen.add(link)
            w0, w1, w2, w3 = (self.w(self.vram, base + 0x80 * k + 2 * link) for k in range(4))
            if w1 != 0xffff: out.append((w0, w1, w2))
            link = w3 & 0x3f
        return out

    def draw_mos(self, mos, layer, tag=None):
        """MAME render_object: 1 tile wide, h tiles tall, code + row, later MOs overwrite."""
        for w0, code, w2 in mos:
            h = (w0 & 15) + 1
            flip = bool(w0 & 0x8000)
            x = (w2 >> 5) & 0x1ff
            top = (-((w0 >> 5) & 0x1ff) - 256 - h * 8) & 0x1ff
            for t in range(h):
                pens, cs = self.mo_tile(code + t)
                for ty in range(8):
                    sy = (top + t * 8 + ty) & 0x1ff
                    if sy >= H: continue
                    for tx in range(8):
                        sx = (x + tx) & 0x1ff
                        if sx >= W: continue
                        pen = pens[ty * 8 + (7 - tx if flip else tx)]
                        if pen: layer[sy][sx] = (bool(w2 & PRIO), pen, 0x100 + cs * 16 + pen, tag)

    def merge(self, pf, layer):
        """atarisy1_v.cpp screen_update (playfield priority pens 0: MOs always over pf)."""
        img = Image.new("RGB", (W, H))
        px = img.load()
        for y in range(H):
            for x in range(W):
                c = pf[y][x]
                mo = layer[y][x]
                if mo:
                    prio, pen, col, _ = mo
                    if not prio: c = col
                    elif pen != 1: c = 0x300 + ((c & 0x0f) << 4) + pen
                px[x, y] = self.pal[c]
        return img


def mask_mos(ram):
    """Player mask entries (+0x38, 5 x code/x/y) -> priority MO words."""
    out = []
    for i in range(5):
        code, x, y = struct.unpack(">Hhh", ram[P - 0x400000 + 0x38 + 6 * i:P - 0x400000 + 0x3e + 6 * i])
        if not code: break
        if not code & 0x4000: return []            # not a mask (respawn sparkle 0x13d38)
        w0 = (((y + MO_Y) & 0x1ff) << 5) | (code & 0x8000) | ((code >> 11) & 7)
        out.append((w0, code & 0x7ff, (((x + MO_X) & 0x1ff) << 5) | PRIO))
    return out


def diff(a, b, box=(0, 24, 288, H)):
    """Differing pixels inside box (default: playfield area without the alpha layer, which
    is not rendered: HUD on top, opaque columns right of 288)."""
    pa, pb = a.load(), b.load()
    x0, y0, x1, y1 = box
    return sum(pa[x, y] != pb[x, y] for y in range(max(0, y0), min(H, y1))
               for x in range(max(0, x0), min(W, x1)))


def marble_box(mos):
    """Screen box around the player's MOs (+-24 px), the area the masks act on."""
    w0, _, w2 = mos[0]
    x = (w2 >> 5) & 0x1ff
    top = (-((w0 >> 5) & 0x1ff) - 256 - ((w0 & 15) + 1) * 8) & 0x1ff
    return (x - 24, max(24, top - 32), min(288, x + 40), top + 48)


def main(argv):
    if len(argv) < 5:
        print(__doc__); return 2
    shots, snapdir, race, outdir = argv[1], argv[2], int(argv[4]), argv[5]
    ticks = ms.load_trace(argv[3])
    os.makedirs(outdir, exist_ok=True)
    rom = ROM
    recs = ms.load_trace(shots)
    pick = [int(a) for a in argv[6:]] or range(len(recs))
    ok = True
    for n in pick:
        fr, ram, vram = recs[n]
        f = Frame(rom, race, ram, vram)
        snap = Image.open(os.path.join(snapdir, "%04d.png" % n)).convert("RGB")
        best = None
        for yoff in range(-16, 17):                  # calibrate the playfield line offset once
            pf = f.playfield(yoff)
            img = f.merge(pf, [[None] * W for _ in range(H)])
            d = diff(img, snap)
            if best is None or d < best[0]: best = (d, yoff, pf)
        _, yoff, pf = best
        renders = {}
        for bank in (0, 1):
            layer = [[None] * W for _ in range(H)]
            f.draw_mos(f.mo_list(bank), layer)
            renders[bank] = f.merge(pf, layer)
        bank = min((0, 1), key=lambda b: diff(renders[b], snap))
        mos = f.mo_list(bank)
        player = mos[:7]                              # 0x05, 0x03, 4 dots, 0x07 (MARBLE.md)
        shown = sorted(m for m in mos[7:] if m[2] & PRIO)
        older = [r for f_, r, _ in ticks if f_ <= fr][-3:]
        lag = next((k for k, r in enumerate(reversed(older)) if sorted(mask_mos(r)) == shown), None)
        src = older[-1 - lag] if lag is not None else ram
        layer = [[None] * W for _ in range(H)]
        f.draw_mos(player, layer)
        nomask = f.merge(pf, layer)
        f.draw_mos(mask_mos(src), layer)
        remake = f.merge(pf, layer)
        box = marble_box(player)
        dm, dr, dn = (diff(im, snap, box) for im in (renders[bank], remake, nomask))
        nm = len(mask_mos(src))
        print("shot %d frame %d: yoff %d bank %d masks %d (tick -%s) | MO RAM render %d px off, "
              "remake rule %d px, no masks %d px (box around the marble), whole playfield %d px"
              % (n, fr, yoff, bank, nm, lag, dm, dr, dn, diff(renders[bank], snap)))
        out = Image.new("RGB", (4 * W + 30, H), (40, 40, 40))
        for k, im in enumerate((snap, renders[bank], remake, nomask)):
            out.paste(im, (k * (W + 10), 0))
        out.resize((out.width * 2, out.height * 2), Image.NEAREST).save(
            os.path.join(outdir, "occl_%02d.png" % n))
        ok &= dm == 0 and dr == 0
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
