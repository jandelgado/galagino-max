#!/usr/bin/env python3
"""Marble Madness title screen model (Phase 6a, MARBLE.md "Attract"): the attract title with
the high score table, the playfield logo and the bouncing marbles (particles), also on the
final screen. Logic from Marble Love (particle-init-18cd2.ts, particle-bounce.ts,
palette-rng-fill-26cfa.ts, high-score-defaults.ts, hi-score-decode-41c8.ts,
late-game-logic-26f3e.ts type 0x2c); sequencer frame counts, title layout, logo map and the
logo palette overlay fitted to MAME captures.

usage: marble_attract.py check TRACEDIR...    frame traces (mame_trace.lua MARBLE_TICK=frame
                                              MARBLE_ALPHA=1, 2026-10-13 record format):
                                              particles, MOs, title cells and palettes
       marble_attract.py logo SHOTSDIR        attract shots (MARBLE_SHOTS): logo playfield RAM
       marble_attract.py fwrite OUT TRACEDIR...  host test fixture (marble_attract_trace.h)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import marble_hud as mh
import marble_sim as ms
from marble_sim import s16

md = mh.md

# --- sequencer (frame counts from MAME traces, +-0) ------------------------------------
ATTRACT = 1
TITLE_LEN = 600       # particles on (0x4003e2 != 0) frames
TITLE_LEAD = 6        # segment / demo end -> title (alpha cleared 1332, title 1338)
TITLE_TAIL = 6        # title end -> next segment (0x4003e4 + 1)
GAME_OVER_TITLE = 14  # 0x400390 = 2 -> title (no high score entry)
GAME_OVER_ATTRACT = 7  # 0x400390 = 2 -> 0x400390 = 1, segment 0
GAME_OVER_RNG = 0x100  # one rng(0x100) at the game over (9 steps)
FINAL_PARTS = 3       # 0x400390 = 6 -> particles (palette fill + init from the frame-0 RNG)

TITLE_COUNT, TITLE_MODE = 3, 0x80   # rng(2) colour per marble
FINAL_COUNT, FINAL_MODE = 28, 0xff  # 8 colours, palette fill 0x26cfa first

# --- particles (0x400a9c, stride 0xa: x, y (1/16 px), vx, vy, mode word) ----------------
PARTS, STRIDE, COUNT = 0x400a9c, 10, 0x4003e2
FILL_TABLE = 0x20bb4   # 8 x 2 variants x 3 words, MO palette sets 0-7 pens 3-5
FILL_FIXED = (0xafff, 0xcfc0)  # pens 1-2


class Ram:
    """Work RAM image with the marble_sim Mem accessors rng_13a98 needs."""
    def __init__(self, ram):
        self.ram = bytearray(ram)

    def rb(self, a): return self.ram[a - 0x400000]
    def wb(self, a, v): self.ram[a - 0x400000] = v & 0xff
    def rw(self, a): return self.ram[a - 0x400000] << 8 | self.ram[a - 0x3fffff]

    def ww(self, a, v):
        self.ram[a - 0x400000] = v >> 8 & 0xff
        self.ram[a - 0x3fffff] = v & 0xff


def fill_26cfa(m):
    """Palette fill: rng(2) per MO colour set 0-7 picks a variant; returns the 8 variants."""
    return [ms.rng_13a98(m, 2) for _ in range(8)]


def fill_palette(r, variants):
    """MO palette words 0x100 + set * 16 + pen (pens 1-5) of the fill."""
    pal = {}
    for k, v in enumerate(variants):
        a = FILL_TABLE + k * 12 + (6 if v else 0)
        words = FILL_FIXED + tuple(md.u16(r, a + 2 * i) for i in range(3))
        for i, w in enumerate(words):
            pal[0x100 + k * 16 + 1 + i] = w
    return pal


def init_18cd2(m, count, mode):
    """Particle init: random position/velocity, mode word (colour) << 11."""
    for i in range(count):
        a = PARTS + i * STRIDE
        m.ww(a, ((ms.rng_13a98(m, 0x100) & 0xff) + 0x24) << 4 & 0xffff)
        m.ww(a + 2, (ms.rng_13a98(m, 0x80) + 0x30) << 4 & 0xffff)
        for o in (4, 6):
            v = ms.rng_13a98(m, 0x60) - 0x30
            m.ww(a + o, (v + (0x10 if v >= 0 else -0x10)) & 0xffff)
        c = mode if mode < 0x80 else ms.rng_13a98(m, 8 if mode == 0xff else 2)
        m.ww(a + 8, c << 11 & 0xffff)
    m.wb(COUNT, count)


def bounce_18dca(m):
    """One frame: move, reflect at x 8..0x148 / y 0..0xe0 (pixels), commit inside only."""
    for i in range(m.rb(COUNT)):
        a = PARTS + i * STRIDE
        x, y, vx, vy = (s16(m.rw(a + o)) for o in (0, 2, 4, 6))
        nx, ny = s16((x + vx) & 0xffff), s16((y + vy) & 0xffff)
        px, py = nx >> 4, ny >> 4
        if (px < 8 and vx < 0) or (px > 0x148 and vx > 0):
            m.ww(a + 4, -vx & 0xffff)
        if (py < 0 and vy < 0) or (py > 0xe0 and vy > 0):
            m.ww(a + 6, -vy & 0xffff)
        if 8 <= px <= 0x148 and 0 <= py <= 0xe0:
            m.ww(a, nx & 0xffff)
            m.ww(a + 2, ny & 0xffff)


def slots(m):
    return [tuple(m.rw(PARTS + i * STRIDE + 2 * k) for k in range(5)) for i in range(m.rb(COUNT))]


def mos(m):
    """MO words (w0, w1, w2) of the particles, 0x26f3e type 0x2c: two 8 x 16 halves, codes
    mode | 1 and mode | 3, MO x = x - 8 / x, MO y = y + 16 (pixels)."""
    out = []
    for i in range(m.rb(COUNT)):
        a = PARTS + i * STRIDE
        x, y, mode = s16(m.rw(a)) >> 4, (s16(m.rw(a + 2)) >> 4) + 0x10, m.rw(a + 8)
        out += [((y << 5) & 0x3fe0 | 1, mode | 1, ((x - 8) << 5) & 0x3fe0),
                ((y << 5) & 0x3fe0 | 1, mode | 3, (x << 5) & 0x3fe0)]
    return out


# --- high score table --------------------------------------------------------------------
DEFAULTS = 0x1eea0           # 10 x 8 B: long score (low 24 bits), 3 ASCII initials, 0
TABLE = 0x401e74 + 0x1e      # RAM: 10 x 5 B: 24 bit score, radix 40 initials word
RADIX = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:;<"


def defaults(r):
    """[(initials, score)] in table order."""
    out = []
    for k in range(10):
        a = DEFAULTS + 8 * k
        out.append((bytes(md._rom(r, a + 4, 3)).decode("ascii"), md.u32(r, a) & 0xffffff))
    return out


def ram_table(m):
    out = []
    for k in range(10):
        a = TABLE + 5 * k
        sc = m.rb(a) << 16 | m.rb(a + 1) << 8 | m.rb(a + 2)
        w = m.rb(a + 3) << 8 | m.rb(a + 4)
        out.append((RADIX[w // 1600 % 40] + RADIX[w // 40 % 40] + RADIX[w % 40], sc))
    return out


# --- title alpha (arcade rows/cols/colours from MAME, remake moves dx/dy) ---------------
S_TROUBLE, S_COIN, S_CREDITS, S_COPYRIGHT = 0x235a8, 0x22f6c, 0x22ff8, 0x22f04


def table_line(rank, initials, score):
    """16 cells from arcade column 13: rank right aligned in 3, initials, score in 9."""
    return "%3s %s%9s" % ("#%d" % rank, initials, "{:,}".format(score))


TITLE_STAGES = 3  # the title draws over 3 frames: table, copyright, coin line


def title_items(table, r, stage=TITLE_STAGES - 1):
    """(dx, dy, row, col, words) of the title, colour 5 / 4, transparent; stage = frames
    since the title started (0: header and table, 1: + copyright, 2: + coin line)."""
    out = []
    put = lambda dx, dy, row, col, ws: out.append((dx, dy, row, col, ws))
    put(-6, 3, 9, 15, mh.text(mh.rom_str(r, S_TROUBLE), 5, False))
    for k, (ini, sc) in enumerate(table):
        put(-6, 3, 11 + k, 13, mh.text(table_line(k + 1, ini, sc), 4, False))
    if stage >= 1:
        put(-6, 3, 29, 12, mh.text(mh.rom_str(r, S_COPYRIGHT), 4, False))
    if stage >= 2:
        put(-2, 3, 28, 3, mh.text(mh.rom_str(r, S_COIN), 5, False))
        put(-7, 3, 28, 24, mh.text(mh.rom_str(r, S_CREDITS) + "0" + " " * 4, 5, False))
    return out


# --- logo: playfield words (captured, MAME attract shots), ROM tiles ---------------------
LOGO_ROW, LOGO_COL = 2, 11   # playfield RAM row / column of the top left word
LOGO = (
    [0xf84] + [0xf85] * 18 + [0xf86],
    list(range(0xf87, 0xf9b)),
    [0xf87] + list(range(0xf9b, 0xfad)) + [0xf9a],
    [0xf87, 0] + list(range(0xfad, 0xfbd)) + [0, 0xf9a],
    [0xf87] + list(range(0xfbd, 0xfce)) + [0, 0xf9a],
    [0xfce] + [0xfcf] * 18 + [0xfd0],
)
LOGO_PAL = 0x20aca           # 13 words over playfield set 0 (palette set 0) at LOGO_PENS
LOGO_PENS = (2, 4, 6, 8, 10, 12, 14, 18, 20, 22, 24, 26, 28)


def logo_palette(r):
    """32 playfield set 0 words of the title: palette set 0 with the overlay."""
    raw = {}
    for k in range(0xC3):
        raw[(md.u32(r, md.PAL_TARGETS + 4 * k) - 0xB00000) // 2] = md.u16(r, md.PAL_SETS + 2 * k)
    pal = [raw[md.PF_PALETTE + i] for i in range(32)]
    for k, pen in enumerate(LOGO_PENS):
        pal[pen] = md.u16(r, LOGO_PAL + 2 * k)
    return pal


def title_mo_palette(r):
    """MO palette words 0x100-0x17f of the title: palette set 0 (ROM)."""
    raw = {}
    for k in range(0xC3):
        raw[(md.u32(r, md.PAL_TARGETS + 4 * k) - 0xB00000) // 2] = md.u16(r, md.PAL_SETS + 2 * k)
    return {i: raw[i] for i in range(0x100, 0x180) if i in raw}


# --- checks ------------------------------------------------------------------------------

def bank_words(mo, bank):
    return [tuple(mo[bank * 256 + k * 64 + i] for k in range(3)) for i in range(64)]


def check_trace(d, r):
    """Particles every frame, MOs, title cells/palette (+-1 frame), sequencer counts."""
    fs = mh.frames(d, mo=True)
    errs, runs = [], []
    m = None
    title_cells = None
    want_title = {}  # frame -> expected alpha dict while the title shows
    for i in range(1, len(fs)):
        f, ram, al, pal, mo, mpal = fs[i]
        prev = fs[i - 1][1]
        n, n0 = ram[COUNT - 0x400000], prev[COUNT - 0x400000]
        phase = mh.rw(ram, mh.GS)
        if n and not n0:
            # init on this frame from the previous frame's RNG (final: fill first, RNG of
            # the 0x400390 = 6 frame)
            if n == FINAL_COUNT:
                k = i
                while mh.rw(fs[k - 1][1], mh.GS) == 6: k -= 1
                if f - fs[k][0] != FINAL_PARTS: errs.append((f, "final particles at +%d" % (f - fs[k][0])))
                m = Ram(fs[k][1])
                variants = fill_26cfa(m)
                init_18cd2(m, FINAL_COUNT, FINAL_MODE)
                for e, w in fill_palette(r, variants).items():
                    if mpal[e - 0x100] != w: errs.append((f, "fill pal %x" % e)); break
            else:
                m = Ram(prev)
                init_18cd2(m, n, TITLE_MODE if n == TITLE_COUNT else 0)
            runs.append([f, n, phase, 0])
        elif not n:
            if m is not None and runs and phase == ATTRACT: runs[-1][3] = f - runs[-1][0]
            m = None
            continue
        elif m is not None:
            bounce_18dca(m)
        if m is None: continue
        if slots(m) != slots(Ram(ram)):
            errs.append((f, "slots"))
            m = Ram(ram)
        # MOs: the bank not selected by 0x4003ae bit 3 holds this frame's particles
        bank = 0 if mh.rw(ram, 0x4003ae) & 8 else 1
        bw = bank_words(mo, bank)
        if not (n and not n0) and not all(w in bw for w in mos(m)): errs.append((f, "mos"))
        if phase == ATTRACT and n == TITLE_COUNT:
            stage = min(f - runs[-1][0], TITLE_STAGES - 1)
            want_title[f] = ({k: w for k, w in cells(title_items(ram_table(Ram(ram)), r, stage))
                              if w & 0x3ff}, len(runs) - 1)
    # title cells: equal to MAME within +-1 frame of each frame the title shows
    fby = {x[0]: x for x in fs}
    starts = {x for x, n, ph, ln in runs}
    for f, (want, run) in want_title.items():
        # a coin / START during the title redraws it (credits) and starts a game: not shown
        # by the remake (no coins), skipped
        if fs[-1][0] > runs[run][0] + TITLE_LEN and runs[run][3] == 0: continue
        ok = False
        for g in (f, f - 1, f + 1):
            if g in fby and mame_cells(fby[g][2]) == want: ok = True
        got = mame_cells(fby[f][2])
        if f in starts and got and all(want.get(k) == w for k, w in got.items()):
            ok = True  # the arcade is still drawing the table in the title's first frame
        if not ok: errs.append((f, "title cells"))
        if f in fby:
            p = fby[f][3]
            alpha_pal = [md.u16(r, md.PAL_ALPHA + 2 * k) for k in range(32)]
            if list(p) != alpha_pal and f + 1 in want_title and f - 1 in want_title:
                errs.append((f, "alpha palette"))
            mp = fby[f][5]
            for e, w in title_mo_palette(r).items():
                if mp[e - 0x100] != w: errs.append((f, "title MO palette %x" % e)); break
    # sequencer: title length; game over -> title
    for f0, n, phase, length in runs:
        if n == TITLE_COUNT and phase == ATTRACT and length and length not in (TITLE_LEN, TITLE_LEN // 2 - 1):
            errs.append((f0, "title length %d" % length))
    for i in range(1, len(fs)):
        if mh.rw(fs[i][1], mh.GS) == 2 and mh.rw(fs[i - 1][1], mh.GS) != 2:
            g = fs[i][0]
            mo_ = Ram(fs[i - 1][1])
            ms.rng_13a98(mo_, GAME_OVER_RNG)
            if mh.rw(fs[i][1], 0x4003a6) != mo_.rw(0x4003a6): errs.append((g, "game over rng"))
            nxt = [x for x, n, ph, _ in runs if x > g]
            entry = any(mh.rw(x[1], mh.GS) == 2 and x[0] > g and x[1][COUNT - 0x400000] == 6
                        for x in fs[i:i + 20])
            if nxt and not entry and nxt[0] - g != GAME_OVER_TITLE:
                errs.append((g, "game over -> title %d" % (nxt[0] - g)))
            att = [x[0] for x in fs[i:] if mh.rw(x[1], mh.GS) == ATTRACT]
            if att and not entry and att[0] - g != GAME_OVER_ATTRACT:
                errs.append((g, "game over -> attract %d" % (att[0] - g)))
    # segments: title end -> next segment
    for k in range(1, len(fs)):
        a, b = fs[k - 1][1], fs[k][1]
        if mh.rb(b, 0x4003e4) != mh.rb(a, 0x4003e4) and mh.rw(b, mh.GS) == ATTRACT:
            ends = [x + ln for x, n, ph, ln in runs if ln == TITLE_LEN and x + ln <= fs[k][0]]
            if ends and fs[k][0] - ends[-1] != TITLE_TAIL:
                errs.append((fs[k][0], "segment after title %d" % (fs[k][0] - ends[-1])))
    return fs, runs, errs


def cells(items):
    out = {}
    for dx, dy, row, col, ws in items:
        for k, w in enumerate(ws):
            out[row, col + k] = w
    return out.items()


def mame_cells(al):
    """Alpha RAM words with a glyph (code != 0), visible columns."""
    return {(row, col): al[row * 64 + col] for row in range(32) for col in range(42)
            if al[row * 64 + col] & 0x3ff}


def check(dirs):
    r = md.load()
    bad = 0
    for d in dirs:
        fs, runs, errs = check_trace(d, r)
        print("%s: %d frames, particle runs %s, %d differ: %s" % (
            d, len(fs), [(f, n, ln) for f, n, ph, ln in runs], len(errs), errs[:6]))
        bad += len(errs)
    return 1 if bad else 0


def check_logo(d):
    """Logo words in the playfield RAM of every title shot equal LOGO."""
    r = md.load()
    R = 8 + 0x2000 + 0x3000
    b = open(os.path.join(d, "shots.bin"), "rb").read()
    n = bad = 0
    for i in range(len(b) // R):
        rec = b[i * R:(i + 1) * R]
        ram, v = rec[8:8 + 0x2000], rec[8 + 0x2000:]
        if mh.rw(ram, mh.GS) != ATTRACT or ram[COUNT - 0x400000] != TITLE_COUNT: continue
        n += 1
        for y, row in enumerate(LOGO):
            words = struct.unpack(">20H", v[((LOGO_ROW + y) * 64 + LOGO_COL) * 2:][:40])
            if list(words) != row: bad += 1
        pal_ok = True
    print("%s: %d title shots, %d logo rows differ" % (d, n, bad))
    pals = open(os.path.join(d, "shots_pal.bin"), "rb").read()
    frs = [int(x) for x in open(os.path.join(d, "shots.txt")).read().split()]
    pbad = 0
    want = logo_palette(r)
    for i, f in enumerate(frs):
        rec = b[i * R:(i + 1) * R]
        ram = rec[8:8 + 0x2000]
        if mh.rw(ram, mh.GS) != ATTRACT or ram[COUNT - 0x400000] != TITLE_COUNT: continue
        p = struct.unpack(">1024H", pals[i * 0x800:(i + 1) * 0x800])
        if list(p[0x200:0x220]) != want: pbad += 1
    print("logo palette: %d shots differ" % pbad)
    return 1 if bad or pbad or not n else 0


def write_fixture(out, dirs):
    """Host test fixture: per particle run the RNG word before the init, count, mode, and
    the slot CRC of every frame (crc32 of the slot words, big endian)."""
    import zlib
    r = md.load()
    runs_out = []
    for d in dirs:
        fs = mh.frames(d)
        for i in range(1, len(fs)):
            ram, prev = fs[i][1], fs[i - 1][1]
            n = ram[COUNT - 0x400000]
            if not n or prev[COUNT - 0x400000]: continue
            if n == FINAL_COUNT:
                k = i
                while mh.rw(fs[k - 1][1], mh.GS) == 6: k -= 1
                rng, mode = mh.rw(fs[k][1], 0x4003a6), FINAL_MODE
            else:
                rng, mode = mh.rw(prev, 0x4003a6), TITLE_MODE if n == TITLE_COUNT else 0
            crcs = []
            for j in range(i, min(i + 700, len(fs))):
                if not fs[j][1][COUNT - 0x400000]: break
                crcs.append(zlib.crc32(fs[j][1][PARTS - 0x400000:PARTS - 0x400000 + n * STRIDE]))
            runs_out.append((os.path.basename(d), fs[i][0], rng, n, mode, crcs))
    with open(out, "w") as f:
        f.write("// generated by romconv/internal/marble/tools/marble_attract.py fwrite\n")
        f.write("// MAME particle runs: RNG word before the init (final: before the palette\n")
        f.write("// fill), count, mode, crc32 of the particle slots every frame from the init\n")
        f.write("#include <stdint.h>\n\n")
        for k, (name, fr, rng, n, mode, crcs) in enumerate(runs_out):
            f.write("static const uint32_t marble_parts_crc%d[] = {\n" % k)
            for j in range(0, len(crcs), 6):
                f.write("  " + ", ".join("0x%08x" % c for c in crcs[j:j + 6]) + ",\n")
            f.write("};\n")
        f.write("struct MarblePartsRun { const char *trace; uint32_t frame; uint16_t rng; "
                "uint8_t count, mode; const uint32_t *crc; uint16_t frames; };\n")
        f.write("static const MarblePartsRun marble_parts_runs[] = {\n")
        for k, (name, fr, rng, n, mode, crcs) in enumerate(runs_out):
            f.write("  {\"%s\", %d, 0x%04x, %d, 0x%02x, marble_parts_crc%d, %d},\n" % (
                name, fr, rng, n, mode, k, len(crcs)))
        f.write("};\n")
    print("wrote", out, len(runs_out), "runs")


def main(argv):
    if len(argv) >= 2 and argv[0] == "check":
        return check(argv[1:])
    if len(argv) == 2 and argv[0] == "logo":
        return check_logo(argv[1])
    if len(argv) >= 3 and argv[0] == "fwrite":
        write_fixture(argv[1], argv[2:])
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
