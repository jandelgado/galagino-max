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

from dev.marble.devrom import ROM, SIM_ROM, md  # noqa: F401
from romconv.games.marble.sim import marble_hud as _model  # noqa: F401
from romconv.games.marble.sim.marble_hud import *  # noqa: F401,F403


# The model takes the decoded ROM set explicitly; the tools use the one loaded by devrom.
def font(): return _model.font(ROM)
def layout_states(r=None): return _model.layout_states(r or ROM)




P = 0x400018                   # player 1
GS, RACE = 0x400390, 0x400394
TIMER, TENTHS, TICKS, ANIM1, ANIM2, SCORE, DEATHS, SKIP = 0x6a, 0x6c, 0x6e, 0x70, 0x71, 0xbc, 0xd2, 0xd8
RACE_ON, GAME_OVER, RACE_END, TIME_OUT, GAME_START, FINAL = 0, 2, 3, 4, 5, 6
RACE_TIME = (60, 60, 35, 30, 20, 20)


def alpha(h, r):
    """Arcade alpha RAM (dict (row, col) -> word) of state h."""
    cells = {}
    for dx, dy, row, col, ws in items(h, r):
        for k, w in enumerate(ws):
            cells[row, col + k] = w
    return cells
# title table of a layout case: 0 ROM defaults, 1 defaults with entry 1 = (blanks, 9,999,999)
TABLE_CASES = 2


# --- MAME traces ------------------------------------------------------------------------

T_REC = 8 + 0x2000
# alpha.bin record: alpha RAM 0x1000 + alpha palette 0x40 (traces before 2026-10-13), then
# MO RAM 0x400 + MO palette 0x200 (mame_trace.lua since 2026-10-13)
A_REC_OLD, A_REC = 0x1040, 0x1640


def frames(d, mo=False):
    """[(frame, work RAM, alpha words, palette words[, MO words, MO palette words])] of a
    trace dir; mo=True needs the 2026-10-13 record format."""
    t = open(os.path.join(d, "trace.bin"), "rb").read()
    a = open(os.path.join(d, "alpha.bin"), "rb").read()
    n = len(t) // T_REC
    size = len(a) // n if n else A_REC
    assert size in (A_REC_OLD, A_REC) and not (mo and size != A_REC), (d, size)
    out = []
    for i in range(n):
        rec = t[i * T_REC:(i + 1) * T_REC]
        al = a[i * size:(i + 1) * size]
        r = (struct.unpack(">I", rec[4:8])[0], rec[8:], struct.unpack(">2048H", al[:0x1000]),
             struct.unpack(">32H", al[0x1000:0x1040]))
        if mo:
            r += (struct.unpack(">512H", al[0x1040:0x1440]), struct.unpack(">256H", al[0x1440:]))
        out.append(r)
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
            # bonus frame: 0x118d2 sets +0x70 = 0 with the line (frame 3), 0xff with the points
            j = i - k + 1 + 2
            while j < n and out[j]._ph == RACE_END and rb(ram[j], P + ANIM1) != 0xff:
                j += 1
            if k >= 3 and i < j:
                h.screen |= BONUS | BONUS_PALETTE | PAL | TOP
                h.value = min(h.seconds, 99) * 100
            elif i >= j:
                h.screen = 0
        if ph == TIME_OUT and k >= 2:
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
    r = ROM
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


HUD_FIELDS = ("screen", "pal3", "pal11", "add", "value")
SYNC_INIT, SYNC_RACE_END = 0, 1
RUN_TAIL = 10                  # race end runs: frames after the next race start


def hud_run(d):
    """(race, sync, frames, steps) of a trace: changes of HUD_FIELDS by frame offset from the
    sync frame (time out run: race init; race end run: first 0x400390 = 3 frame)."""
    fs = frames(d)
    hs = hud_states(fs)
    ph = [rw(f[1], GS) for f in fs]
    if RACE_END in ph:
        sync, kind = ph.index(RACE_END), SYNC_RACE_END
        on = next((i for i in range(sync, len(fs)) if ph[i] == RACE_ON), None)
        end = on + RUN_TAIL if on else next(i for i in range(sync, len(fs)) if ph[i] == GAME_OVER) + 3
    else:
        sync = next(i for i, f in enumerate(fs) if rb(f[1], P + TENTHS) == 9 and rb(f[1], P + TICKS) == 0xff)
        kind = SYNC_INIT
        end = next(i for i in range(sync, len(fs)) if ph[i] == GAME_OVER) + 2
    m = fs[sync][1]
    race = rw(m, RACE) if kind == SYNC_RACE_END else 0
    inputs = (sw(m, P + TIMER), rl(m, P + SCORE), rw(m, P + DEATHS), rl(m, 0x400010))
    steps, last = [], None
    for i in range(sync, end):
        v = tuple(getattr(hs[i], n) for n in HUD_FIELDS)
        if v != last:
            steps.append((i - sync,) + v)
            last = v
    return race, kind, end - sync, steps, inputs


def write_fixture(out, dirs):
    runs = [hud_run(d) for d in dirs]
    with open(out, "w") as f:
        w = lambda s="": print(s, file=f)
        w("// Generated by romconv/dev/marble/make_fixture.py hwrite; do not edit.")
        w("// Marble HUD host test (MARBLE.md \"HUD\"): HUD state per frame of MAME frame traces")
        w("// (dev/marble/mame_trace.lua MARBLE_TICK=frame MARBLE_ALPHA=1, marble_hud.py hud_states, equal")
        w("// to the MAME alpha layer +-1 frame). Steps: frame offset from the sync frame and the new")
        w("// screen bits, pulse indices, intro seconds to add, bonus / final value. Sync: race init")
        w("// (time out run from a new game) or the first 0x400390 = 3 frame (race end runs, inputs")
        w("// at the sync frame: seconds +0x6a, score +0xbc, deaths +0xd2, 0x400010).")
        w("#ifndef MARBLE_HUD_TRACE_H\n#define MARBLE_HUD_TRACE_H\n#include <stdint.h>\n")
        w("struct MarbleHudStep { uint16_t at, screen; uint8_t pal3, pal11, add; int32_t value; };")
        w("struct MarbleHudRun {")
        w("  uint8_t race, sync; uint16_t frames, nsteps; const MarbleHudStep *steps;")
        w("  int16_t seconds; uint32_t score; uint16_t deaths; uint32_t frames0;")
        w("};")
        w("static const uint8_t MARBLE_HUD_SYNC_INIT = %d, MARBLE_HUD_SYNC_RACE_END = %d;"
          % (SYNC_INIT, SYNC_RACE_END))
        for k, (race, kind, n, steps, _) in enumerate(runs):
            w("static const MarbleHudStep marble_hud_steps%d[] = {" % k)
            for st in steps:
                w("  {%d, 0x%x, %d, %d, %d, %d}," % st)
            w("};")
        w("static const MarbleHudRun marble_hud_runs[] = {")
        for k, (race, kind, n, steps, inp) in enumerate(runs):
            w("  {%d, %d, %d, %d, marble_hud_steps%d, %d, %d, %d, 0x%x}," % ((race, kind, n, len(steps), k) + inp))
        w("};\n#endif")


def main(argv):
    if len(argv) >= 2 and argv[0] == "check":
        sys.exit(1 if check(argv[1:]) else 0)
    print(__doc__)
    sys.exit(2)


if __name__ == "__main__":
    main(sys.argv[1:])
