#!/usr/bin/env python3
"""Marble Madness HUD model (Phase 3b, MARBLE.md "HUD"): the alpha layer (0xa03000, 64 x 32
words, font 136032.104.f5) and its 32 palette words, built from a compact HUD state instead
of the 68k text OS. Texts are the ROM strings, big glyph tile runs are captured from MAME
(the BIOS big font tables 0x72ac/0x0ccc are not ported). Palette pulses: anim ticks 0x26bee
(+0x70, entry 3, score fade) and 0x26c78 (+0x71, entry 11, timer flash); bonus palette
0x118d2.

The state comes from MAME frame traces here (hud_states) and from the work RAM image and
the flow sequencer in the remake (marble_hud.cpp). Arcade coordinates (alpha) are checked
against MAME; the remake shows the same items moved into 30 x 36 cells (view).

usage: marble_hud.py check TRACEDIR...  frame traces (mame_trace.lua MARBLE_TICK=frame
                                        MARBLE_ALPHA=1): cells and palette per frame, +-1
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import marble_data as md

ALPHA_ZIP = os.path.join(md.HERE, "..", "..", "..", "romszip", "atarisy1.zip")
FONT_ROM, FONT_SHA1 = "136032.104.f5", "72ba464da01bd6d3a91b8d9997d5ac14b6f47aad"

ROWS, COLS = 32, 64            # alpha RAM
VIEW_ROWS, VIEW_COLS = 36, 30  # remake grid (240 x 288)
OPAQUE = 0x2000

P = 0x400018                   # player 1
GS, RACE = 0x400390, 0x400394
TIMER, TENTHS, TICKS, ANIM1, ANIM2, SCORE, DEATHS, SKIP = 0x6a, 0x6c, 0x6e, 0x70, 0x71, 0xbc, 0xd2, 0xd8
RACE_ON, GAME_OVER, RACE_END, TIME_OUT, GAME_START, FINAL = 0, 2, 3, 4, 5, 6
RACE_TIME = (60, 60, 35, 30, 20, 20)

# ROM strings (NUL terminated, ' ' = code 0)
S_YOU, S_THE, S_BONUS_FIN, S_SEC, S_DIED, S_TOTAL = 0x230a0, 0x230b4, 0x230c8, 0x230e8, 0x230f8, 0x23114
S_BLANK26, S_TIME_TO, S_EXTRA, S_RACES, S_WRONG, S_BLANK31 = 0x2317c, 0x23198, 0x231ec, 0x23224, 0x232ec, 0x2330c
S_NAMES = (0x2332c, 0x23336, 0x23340, 0x2334e, 0x23356, 0x2335c)
S_BONUS_FOR, S_TIME_LEFT, S_OUT, S_OVER, S_PUSH = 0x233ae, 0x233c0, 0x2370a, 0x2371c, 0x23004
RACE_STRIDE = 0x1c

PULSE = 0x20b34                # 16 words, the same at 0x20b74 (timer flash)
BONUS_PAL = {0: 0, 4: 0, 8: 0, 12: 0, 9: 0xafff, 13: 0xafff, 29: 0}  # 0x118d2

# big font (2 rows of tiles; digits 0x100 + 4d: TL TR / BL BR, comma one column)
BLANK, COMMA_LO, COLON = 0x13c, 0x16e, 0x16d
BIG = {   # captured from MAME (alpha RAM, final screen)
    "CONGRATULATIONS!": (
        (0x100, 0x12f, 0x100, 0x101, 0x148, 0x149, 0x100, 0x12f, 0x12c, 0x121, 0x128, 0x129, 0x152,
         0x153, 0x131, 0x154, 0x131, 0x142, 0x128, 0x129, 0x152, 0x153, 0x139, 0x13a, 0x100, 0x101,
         0x148, 0x149, 0x14e, 0x14f, 0x16f),
        (0x102, 0x130, 0x102, 0x103, 0x14a, 0x14b, 0x102, 0x136, 0x12d, 0x141, 0x12a, 0x12b, 0x106,
         0x107, 0x155, 0x156, 0x132, 0x143, 0x12a, 0x12b, 0x106, 0x107, 0x106, 0x13b, 0x102, 0x103,
         0x14a, 0x14b, 0x150, 0x151, 0x170)),
    "FINAL": (
        (0x12c, 0x133, 0x139, 0x13a, 0x148, 0x149, 0x128, 0x129, 0x131, 0x142),
        (0x12d, 0x135, 0x106, 0x13b, 0x14a, 0x14b, 0x12a, 0x12b, 0x132, 0x143)),
    "SCORE:": (
        (0x14e, 0x14f, 0x100, 0x12f, 0x100, 0x101, 0x12c, 0x121, 0x12c, 0x133, 0x16d),
        (0x150, 0x151, 0x102, 0x130, 0x102, 0x103, 0x12d, 0x141, 0x12d, 0x134, 0x16d)),
}

# screen bits
PAL, TOP, INTRO, SILLY, BONUS, BONUS_PALETTE, OUT, FIN, PUSH = (1 << k for k in range(9))


def font():
    """512 glyphs, 8 rows of 8 pens 0-3 (anlayout: planes at bits 0 and 4, x bits 0-3, 8-11)."""
    d = bytes(md.load_file(ALPHA_ZIP, [FONT_ROM], FONT_SHA1))
    xs = (0, 1, 2, 3, 8, 9, 10, 11)
    bit = lambda g, n: (d[g * 16 + (n >> 3)] >> (7 - (n & 7))) & 1
    return [[[bit(g, y * 16 + x) << 1 | bit(g, y * 16 + 4 + x) for x in xs] for y in range(8)]
            for g in range(512)]


def rom_str(r, addr):
    s = bytearray()
    while md._rom(r, addr + len(s), 1)[0]:
        s.append(md._rom(r, addr + len(s), 1)[0])
    return s.decode()


def word(code, colour, opaque):
    return code | colour << 10 | (OPAQUE if opaque else 0)


def text(s, colour, opaque):
    return [word(0 if ch == " " else ord(ch), colour, opaque) for ch in s]


def commas(v):
    return "{:,}".format(v)


def big_digits(s, colour, opaque=False):
    """Big number string (digits, commas, spaces) -> two rows of words."""
    top, bot = [], []
    for ch in s:
        if ch == ",":
            top.append(BLANK); bot.append(COMMA_LO)
        elif ch == " ":                  # digit wide
            top += [BLANK, BLANK]; bot += [BLANK, BLANK]
        else:
            t = 0x100 + 4 * int(ch)
            top += [t, t + 1]; bot += [t + 2, t + 3]
    return [word(c, colour, opaque) for c in top], [word(c, colour, opaque) for c in bot]


class Hud:
    """Compact HUD state (marble_video::Hud in the remake)."""
    def __init__(self, **kw):
        self.screen, self.score, self.seconds, self.value = 0, 0, 0, 0
        self.race, self.add, self.deaths = 0, 0, 0
        self.pal3 = self.pal11 = 0xff   # pulse table index, 0xff = ROM colour
        self.__dict__.update(kw)

    def key(self):
        return tuple(sorted(self.__dict__.items()))


def items(h, r):
    """Alpha items of state h: (dx, dy, row, col, words) in arcade coordinates; dx/dy move them
    into the remake's 30 x 36 view (banners: arcade screen 3 rows lower, centred)."""
    s = h.screen
    out = []
    put = lambda dx, dy, row, col, ws: out.append((dx, dy, row, col, ws))
    if s & TOP:  # first: the bonus line covers a timer border cell, as in the arcade
        border = word(0x5f, 7, False)
        put(-3, 0, 0, 3, [border] * 5)
        put(-3, 0, 1, 3, text("SCORE", 0, True))
        put(-3, 0, 2, 3, text(commas(h.score), 0, True))
        top, bot = big_digits("%02d" % max(0, min(h.seconds, 99)), 2, True)
        put(-6, 0, 0, 19, [border] * 4 + [word(0xff, 7, False)])
        put(-6, 0, 1, 19, top + [word(0xdf, 7, False)])
        put(-6, 0, 2, 19, bot + [word(0xdf, 7, False)])
        put(-6, 0, 3, 19, [word(0x5e, 7, False)] * 4 + [word(0x1b, 7, False)])
    if s & INTRO:
        name = rom_str(r, S_NAMES[h.race])
        line = rom_str(r, S_RACES + RACE_STRIDE * h.race)
        cs = text(line, 4, True)
        at = line.index(name)
        cs[at:at + len(name)] = text(name, 5, True)
        put(-6, 3, 8, 8, text(rom_str(r, S_BLANK26), 4, True))
        put(-6, 3, 9, 8, text(rom_str(r, S_TIME_TO if h.race < 2 else S_EXTRA), 4, True))
        put(-6, 3, 10, 8, cs)
        put(-6, 3, 11, 8, text(rom_str(r, S_BLANK26), 4, True))
        top, bot = big_digits("%2d" % h.add, 5, True)
        put(-6, 3, 9, 29, top)
        put(-6, 3, 10, 29, bot)
        if s & SILLY:
            for row, a in ((14, S_BLANK31), (15, S_WRONG), (16, S_BLANK31)):
                put(-6, 3, row, 6, text(rom_str(r, a), 5, True))
    if s & BONUS:
        put(-3, 0, 3, 3, text(rom_str(r, S_BONUS_FOR), 0, True))
        line = rom_str(r, S_TIME_LEFT)
        v = commas(h.value)
        put(-3, 0, 4, 3, text(line[:len(line) - len(v)] + v, 0, True))
    if s & OUT:
        blank = " " * 16
        for row, t in ((24, blank), (25, rom_str(r, S_OUT)), (26, blank), (27, rom_str(r, S_OVER)),
                       (28, blank)):
            put(-6, 3, row, 13, text(t, 0, True))
    if s & PUSH:
        put(-6, 3, 24, 8, text(rom_str(r, S_PUSH), 0, False))
    if s & FIN:
        sec = min(h.seconds, 99)
        cong = BIG["CONGRATULATIONS!"]
        put(-6, 3, 2, 6, [word(c, 4, False) for c in cong[0]])
        put(-6, 3, 3, 6, [word(c, 4, False) for c in cong[1]])
        put(-6, 3, 7, 12, text(rom_str(r, S_YOU), 5, False))
        put(-6, 3, 8, 12, text(rom_str(r, S_THE), 5, False))
        fin = rom_str(r, S_BONUS_FIN)
        label = fin.rstrip("0123456789,")
        put(-7, 3, 13, 9, text(label.rstrip(), 5, False))
        put(-9, 3, 13, 9 + len(label), text(fin[len(label):], 5, False))
        put(-7, 3, 15, 10, text("%2d" % h.seconds, 4, False))
        put(-7, 3, 15, 13, text(rom_str(r, S_SEC), 5, False))
        put(-9, 3, 15, 33, text("%6s" % commas(sec * 1000), 5, False))
        died = rom_str(r, S_DIED)
        put(-7, 3, 17, 7, text(died[:-1].rstrip(), 5, False))
        put(-7, 3, 17, 12, text("%2d" % h.deaths, 4, False))
        put(-9, 3, 17, 32, text(died[-1:], 5, False))
        put(-9, 3, 17, 33, text("%6s" % commas(min(h.deaths, 20) * 1000), 5, False))
        put(-7, 3, 19, 23, text(rom_str(r, S_TOTAL), 5, False))
        put(-9, 3, 19, 32, text("%7s" % commas(h.value), 5, False))
        f = BIG["FINAL"]
        put(-4, 3, 22, 4, [word(c, 4, False) for c in f[0]])
        put(-4, 3, 23, 4, [word(c, 4, False) for c in f[1]])
        sc = BIG["SCORE:"]
        put(-10, 3, 24, 12, [word(c, 4, False) for c in sc[0]])
        put(-10, 3, 25, 12, [word(c, 4, False) for c in sc[1]])
        top, bot = big_digits(commas(h.score), 4)
        put(-10, 3, 24, 40 - len(top), top)
        put(-10, 3, 25, 40 - len(bot), bot)
    return out


def alpha(h, r):
    """Arcade alpha RAM (dict (row, col) -> word) of state h."""
    cells = {}
    for dx, dy, row, col, ws in items(h, r):
        for k, w in enumerate(ws):
            cells[row, col + k] = w
    return cells


def view(h, r):
    """Remake grid: VIEW_ROWS x VIEW_COLS words, items moved by (dx, dy), clipped."""
    g = [[0] * VIEW_COLS for _ in range(VIEW_ROWS)]
    for dx, dy, row, col, ws in items(h, r):
        for k, w in enumerate(ws):
            y, x = row + dy, col + k + dx
            if 0 <= y < VIEW_ROWS and 0 <= x < VIEW_COLS:
                g[y][x] = w
    return g


def palette(h, r):
    """32 alpha palette words (IRGB) of state h."""
    if not h.screen & PAL:
        return [0] * 32
    pal = [md.u16(r, md.PAL_ALPHA + 2 * i) for i in range(32)]
    if h.screen & BONUS_PALETTE:
        for i, v in BONUS_PAL.items():
            pal[i] = v
    if h.pal3 != 0xff:
        pal[3] = md.u16(r, PULSE + 2 * h.pal3)
    if h.pal11 != 0xff:
        pal[11] = md.u16(r, PULSE + 2 * h.pal11)
    return pal


# --- MAME traces ------------------------------------------------------------------------

A_REC, T_REC = 0x1040, 8 + 0x2000


def frames(d):
    """[(frame, work RAM, alpha words, palette words)] of a trace dir."""
    t = open(os.path.join(d, "trace.bin"), "rb").read()
    a = open(os.path.join(d, "alpha.bin"), "rb").read()
    out = []
    for i in range(len(t) // T_REC):
        rec = t[i * T_REC:(i + 1) * T_REC]
        al = a[i * A_REC:(i + 1) * A_REC]
        out.append((struct.unpack(">I", rec[4:8])[0], rec[8:], struct.unpack(">2048H", al[:0x1000]),
                    struct.unpack(">32H", al[0x1000:])))
    return out


def rb(m, a): return m[a - 0x400000]
def rw(m, a): return struct.unpack(">H", m[a - 0x400000:a - 0x3ffffe])[0]
def sw(m, a): return struct.unpack(">h", m[a - 0x400000:a - 0x3ffffe])[0]
def rl(m, a): return struct.unpack(">I", m[a - 0x400000:a - 0x3ffffc])[0]


def hud_states(fs):
    """HUD state per trace frame from work RAM alone (the remake derives the same from its RAM
    image and the flow counters): offsets from the race init (+0x6c = 9, +0x6e = 0xff), the
    first +5 s, the 0x400390 changes."""
    n = len(fs)
    ram = [f[1] for f in fs]
    phase = [rw(m, GS) for m in ram]
    since = [0] * n                  # frames since the phase changed (1 = first frame)
    for i in range(n):
        since[i] = 1 if i == 0 or phase[i] != phase[i - 1] else since[i - 1] + 1
    init = [rb(m, P + TENTHS) == 9 and rb(m, P + TICKS) == 0xff for m in ram]
    starts = [i for i in range(n) if init[i] and (i == 0 or not init[i - 1])]
    # intro of each init: first +5 s (banner 60 frames before), race start
    intro = {}
    for s in starts:
        first = next(i for i in range(s, n) if sw(ram[i], P + TIMER) - sw(ram[i - 1], P + TIMER) == 5)
        on = next(i for i in range(s, n) if phase[i] == RACE_ON)
        intro[s] = (first, on)
    out = []
    pal3 = pal11 = 0xff
    last_init = None
    final_score = None
    for i, m in enumerate(ram):
        if i in intro:
            last_init = i
        h = Hud(score=rl(m, P + SCORE), seconds=sw(m, P + TIMER), deaths=rw(m, P + DEATHS),
                race=rw(m, RACE))
        ph, k = phase[i], since[i]
        scr = 0
        if last_init is not None:
            first, on = intro[last_init]
            loaded = i >= min(last_init + 4, first - 60)  # palette load, at the latest with the banner
            if loaded and ph in (RACE_ON, GAME_START, RACE_END, TIME_OUT) or ph == GAME_OVER and k < 2:
                scr |= PAL | TOP
            if first - 60 <= i < on:
                scr |= INTRO | (SILLY if h.race == 4 else 0)
                h.add = RACE_TIME[h.race] - (h.seconds - sw(ram[first - 1], P + TIMER))
        if ph == RACE_END:
            bonus_at = None
        out.append(h)
        h.screen = scr
        h._ph, h._k = ph, k
    # race end, final, time out: from the phase changes
    for i, h in enumerate(out):
        ph, k = h._ph, h._k
        if ph == RACE_END and not h.screen & INTRO:
            # bonus frame: the score jump of min(s, 99) * 100 (0x118d2)
            j = i - k + 1
            while j < n and out[j]._ph == RACE_END and rl(ram[j], P + SCORE) == rl(ram[j - 1], P + SCORE):
                j += 1
            if k >= 3 and i < j:
                h.screen |= BONUS | BONUS_PALETTE | PAL | TOP
                h.value = min(h.seconds, 99) * 100
            elif i >= j:
                h.screen = 0
        if ph == TIME_OUT and k >= 2 or ph == GAME_OVER and k < 1 and i and out[i - 1]._ph == TIME_OUT:
            h.screen |= OUT | PAL | TOP
        if ph == FINAL:
            if final_score is None:
                j = i
                final_score = rl(ram[i], P + SCORE)
                total = 20000 + min(h.seconds, 99) * 1000 - min(h.deaths, 20) * 1000
            h.screen = PAL | (FIN if k >= 11 else 0)
            h.value = total - (h.score - final_score)
        if ph == GAME_OVER and i and k < 3 and (out[i - k]._ph if i - k >= 0 else 0) == FINAL:
            h.screen = PAL | FIN
            h.value = 0
    # palette pulses (anim ticks run before the timer in the vblank: RAM shows ctr + 1)
    for i, h in enumerate(out):
        m = ram[i]
        if not h.screen & PAL or i and not out[i - 1].screen & PAL:
            pal3 = pal11 = 0xff
        c1, c2 = rb(m, P + ANIM1), rb(m, P + ANIM2)
        if c1 not in (0, 0xff) and rb(m, P + SKIP) == 0:
            pal3 = ((c1 - 1) & 0x3f) >> 2
        if c2 == 0xff:
            pal11 = 0xff
        elif c2:
            pal11 = ((c2 - 1) & 0x1f) >> 1
        h.pal3, h.pal11 = pal3, pal11
    for h in out:
        del h._ph, h._k
    return out


def normal(cells, glyphs):
    """Visible content: blank transparent cells (code 0 or an empty glyph) dropped."""
    out = {}
    for (row, col), w in cells.items():
        code = w & 0x3ff
        if not w & OPAQUE and (code == 0 or not any(any(line) for line in glyphs[code])):
            continue
        out[row, col] = w if code or w & OPAQUE else 0
    return out


def mame_cells(w):
    return {(row, col): w[row * COLS + col] for row in range(30) for col in range(3, 40)
            if w[row * COLS + col] and not (col == 39 and w[row * COLS + col] == word(0, 5, True))}


def check(dirs):
    r = md.load()
    glyphs = font()
    bad = 0
    for d in dirs:
        fs = frames(d)
        hs = hud_states(fs)
        model = [(normal(alpha(h, r), glyphs), palette(h, r)) for h in hs]
        mame = [(normal(mame_cells(f[2]), glyphs) if any(f[3]) else {}, list(f[3])) for f in fs]
        miss = []
        first = next(i for i, f in enumerate(fs) if rb(f[1], P + TENTHS) == 9 and rb(f[1], P + TICKS) == 0xff)
        for i, (cells, pal) in enumerate(model):
            if i < first or pal == [0] * 32 and mame[i][1][1] == 0:
                continue                     # arcade game start (the remake starts from START)
            if rw(fs[i][1], GS) in (GAME_OVER, 1) and not hs[i].screen:
                continue                     # attract mode after game over (Phase 6)
            near = [mame[j] for j in (i - 1, i, i + 1) if 0 <= j < len(mame)]
            if not any(c == cells and p == pal for c, p in near):
                miss.append(i)
        runs = []
        for i in miss:
            if runs and runs[-1][1] == i - 1:
                runs[-1][1] = i
            else:
                runs.append([i, i])
        print("%s: %d frames, %d differ: %s" % (d, len(fs), len(miss),
              " ".join("%d-%d" % (fs[a][0], fs[b][0]) for a, b in runs)))
        for i in miss[:8]:
            cells, pal = model[i]
            mc, mp = mame[i]
            diff = sorted(set(cells.items()) ^ set(mc.items()))[:6]
            pd = [(k, hex(pal[k]), hex(mp[k])) for k in range(32) if pal[k] != mp[k]][:6]
            print("  frame %d screen %x: cells %s pal %s" % (fs[i][0], hs[i].screen, diff, pd))
        bad += len(miss)
    return bad


def main(argv):
    if len(argv) >= 2 and argv[0] == "check":
        sys.exit(1 if check(argv[1:]) else 0)
    print(__doc__)
    sys.exit(2)


if __name__ == "__main__":
    main(sys.argv[1:])
