#!/usr/bin/env python3
"""Marble Madness attract demo model (Phase 6b, MARBLE.md "Attract"): the demo segment of
the attract sequencer (level load wait, race init, timer start, waypoint list end, title) and
the demo race: the player steered by the waypoint lists (marble_sim.waypoint_1815a, Marble
Love waypoint-list-step-1815a.ts), banners through the BIOS message slot
(state-machine-schedule.ts). Frame counts fitted to MAME frame traces.

usage: marble_demo.py check TRACEDIR...    frame traces (mame_trace.lua MARBLE_TICK=frame
                                           MARBLE_ALPHA=1; MARBLE_ATTRACT=1 for boot attract)
       marble_demo.py fwrite OUT TRACEDIR...  host test fixture (marble_demo_trace.h)
"""
import os
import sys

from dev.marble import marble_enemy
from dev.marble import marble_flow as mf
from dev.marble import marble_hud as mh
from dev.marble import marble_sim as ms

P = 0x400018
GS, SEGMENT, RACE, FRAMES, RNG, PARTS = 0x400390, 0x4003e4, 0x400394, 0x400010, 0x4003a6, 0x4003e2
CAMERA = 0x40097c
ATTRACT = 1

# demo segment, frames (MAME, every segment the same)
LOAD = (59, 84)       # segment change (title tail end) to the race init, black
START = (3, 4)        # race init to the first logic tick and the timer start (60 s)
TITLE_AFTER_END = 11  # waypoint list end (0x40075a = 1) to the title marbles
DEMO_SECONDS = 60
FRAMES_AT_INIT = 1    # 0x400010 at the race init
FRAMES_TICK0 = 3      # 0x400010 in the first logic tick, + 1 per tick (counts ticks)

# player fields compared per tick (timer bytes are the vblank's: compared per frame)
KEY = tuple(k for k in mf.KEY if not 0x6a <= k <= 0x6e)


def demos(fs):
    """Demo segments of a trace: dict seg, race, segf (segment change), init (slot 3 -> 1),
    tick (timer start), end (list end), title (marbles), ticks [(frame, ram)]."""
    out, cur, prev = [], None, None
    for i, (f, r, al, pal) in enumerate(fs):
        if mh.rw(r, GS) != ATTRACT:
            prev = None; cur = None; continue
        if prev is not None and mh.rb(r, SEGMENT) != mh.rb(prev, SEGMENT) and cur is None:
            cur = {"seg": mh.rb(r, SEGMENT), "race": mh.rw(r, RACE), "segf": f, "ticks": []}
        if cur is not None:
            if "init" not in cur and mh.rb(prev, P + 0x18) == 3 and mh.rb(r, P + 0x18) == 1:
                cur["init"] = f; cur["init_ram"] = fs[i][1]
            elif "init" in cur and not cur.get("done") and mh.rw(r, RNG) != mh.rw(prev, RNG):
                cur.setdefault("tick", f)
                cur["ticks"].append((f, r))   # tick end (rng 0x118a4 last)
                cur["done"] = "end" in cur
            if "tick" in cur and "end" not in cur and mh.rw(r, ms.WAYPOINTS_END) == 1:
                cur["end"] = f                # within a tick that may end a frame later
                cur["done"] = mh.rw(r, RNG) != mh.rw(prev, RNG)
            if "end" in cur and mh.rw(r, PARTS):
                cur["title"] = f
                out.append(cur); cur = None
        prev = r
    return out


def ram_key(r):
    return (tuple(mh.rb(r, P + k) for k in KEY), mh.rl(r, ms.WAYPOINT), mh.rw(r, ms.WAYPOINTS_END),
            mh.rw(r, RNG))


def mem_key(m):
    return (tuple(m.rb(P + k) for k in KEY), m.rl(ms.WAYPOINT), m.rw(ms.WAYPOINTS_END), m.rw(RNG))


def replay(d, m):
    """Logic ticks of demo d on m (main_tick, camera and 0x400010 from the trace); returns
    the first differing tick (index, frame, field) or None, and the banner set frames."""
    banners = []
    for k, (f, r) in enumerate(d["ticks"]):
        chain = m.rl(ms.MSG_CHAIN)
        m.wl(ms.MSG_CHAIN, 0)
        ms.main_tick(m, 0, 0, mh.rl(r, CAMERA), FRAMES_TICK0 + k)
        if m.rl(ms.MSG_CHAIN): banners.append((f, m.rl(ms.MSG_CHAIN)))
        else: m.wl(ms.MSG_CHAIN, chain)
        a, b = mem_key(m), ram_key(r)
        if a != b:
            if a[0] != b[0]:
                i = next(i for i in range(len(KEY)) if a[0][i] != b[0][i])
                return (k, f, "player +0x%02x: %02x != MAME %02x" % (KEY[i], a[0][i], b[0][i])), banners
            return (k, f, "cursor/end/rng %s != MAME %s" % (a[1:], b[1:])), banners
    return None, banners


def mame_banners(fs, d):
    out, prev = [], 0
    for f, r, al, pal in fs:
        if not d["init"] <= f <= d["end"]: continue
        c = mh.rl(r, ms.MSG_CHAIN)
        if c and c != prev: out.append((f, c))
        prev = c
    return out


def remake_start(d, rng):
    """The remake's demo race: cleared RAM with the RNG, race start, waypoint cursor, demo
    state (0x400390 = 1), 0x400010."""
    m = mf.next_race(d["race"], rng, ((b"\0" * 6), (b"\0" * 4), (b"\0" * 2)), FRAMES_AT_INIT)
    m.ww(GS, ATTRACT)
    ms.demo_start(m, d["race"])
    return m


HUD_TO = 5   # waypoint list end to the alpha palette off


def demo_hud(d, f, r, chains):
    """HUD state of demo frame f: race top items and the coin box from the first tick to the
    list end + 5, the banner of the message slot."""
    h = mh.Hud(score=mh.rl(r, P + 0xbc), seconds=mh.sw(r, P + 0x6a), race=d["race"])
    if d["tick"] <= f < d["end"] + HUD_TO:
        h.screen = mh.PAL | mh.TOP | mh.DEMO
        c = mh.rl(r, ms.MSG_CHAIN)
        h.banner = chains.index(c) if c else mh.NO_BANNER
    return h


def check_hud(fs, d, r0, glyphs):
    """Alpha cells and palette of the demo frames against MAME, +-1 frame."""
    chains = [ms.rom_l(ms.BANNERS + 4 * b) for b in range(7)]
    idx = {f: i for i, (f, *_) in enumerate(fs)}
    lo, hi = idx[d["segf"]], idx[d["end"] + HUD_TO + 3]
    model = []
    for f, r, al, pal in fs[lo:hi]:
        h = demo_hud(d, f, r, chains)
        model.append((f, mh.normal(mh.alpha(h, r0), glyphs), mh.palette(h, r0)))
    mame = [(mh.normal(mh.mame_cells(al), glyphs) if any(pal) else {}, list(pal))
            for f, r, al, pal in fs[lo - 1:hi + 1]]
    miss = [f for k, (f, cells, pal) in enumerate(model)
            if not any(c == cells and p == pal for c, p in mame[k:k + 3])]
    return miss


def check_trace(path):
    fs = mh.frames(path)
    r0 = mh.ROM
    glyphs = mh.font()
    ok = True
    for d in demos(fs):
        t = (d["init"] - d["segf"], d["tick"] - d["init"], d["end"] - d["tick"], d["title"] - d["end"])
        want = (LOAD[d["race"]], START[d["race"]], None, TITLE_AFTER_END)
        good = all(w is None or w == g for w, g in zip(want, t))
        ok &= good
        print("%s seg %d race %d: load %d start %d run %d title %d%s" % (
            path, d["seg"], d["race"], *t, "" if good else " DIFFERS"))
        pre = next(r for f, r, al, pal in fs if f == d["tick"] - 1)
        want_b = mame_banners(fs, d)
        # from the remake's race start (banners +-1 frame: MAME ticks take 1-3 frames)
        m = remake_start(d, mh.rw(pre, RNG))
        bad, got = replay(d, m)
        near = len(got) == len(want_b) and all(
            a[1] == b[1] and abs(a[0] - b[0]) <= 1 for a, b in zip(got, want_b))
        print("  remake start: %s, banners %s" % (
            "equal" if bad is None else "differ at %s" % (bad,),
            "equal +-1" if near else "%s != MAME %s" % (got, want_b)))
        ok &= bad is None and near
        miss = check_hud(fs, d, r0, glyphs)
        print("  hud: %s" % ("equal +-1" if not miss else "%d frames differ: %s" % (len(miss), miss[:10])))
        ok &= not miss
    return ok


def crc32(b):
    import zlib
    return zlib.crc32(bytes(b))


def write_fixture(out, paths):
    """Per demo race (first segment of each in the traces): the RNG at the race init, per
    tick the player position, state, cursor, RNG and a CRC32 of the KEY bytes, the banners
    (tick index, banner number)."""
    runs = {}
    for path in paths:
        fs = mh.frames(path)
        for d in demos(fs):
            if d["race"] in runs: continue
            pre = next(r for f, r, al, pal in fs if f == d["tick"] - 1)
            m = remake_start(d, mh.rw(pre, RNG))
            bad, got = replay(d, m)
            assert bad is None, bad
            chains = [ms.rom_l(ms.BANNERS + 4 * b) for b in range(7)]
            ticks = {f: k for k, (f, r) in enumerate(d["ticks"])}
            runs[d["race"]] = (mh.rw(pre, RNG), d["ticks"],
                               [(ticks[f], chains.index(c)) for f, c in got])
    with open(out, "w") as f:
        print("// Generated by romconv/dev/marble/marble_demo.py fwrite; do not edit.", file=f)
        print("// MAME attract demo races (MARBLE.md \"Attract\"): counts, words and CRC32s only.", file=f)
        print("#include <stdint.h>", file=f)
        print("static const uint8_t MARBLE_DEMO_LOAD[2] = { %d, %d };" % LOAD, file=f)
        print("static const uint8_t MARBLE_DEMO_START[2] = { %d, %d };" % START, file=f)
        print("static const uint8_t MARBLE_DEMO_TITLE_AFTER_END = %d;" % TITLE_AFTER_END, file=f)
        print("static const uint8_t MARBLE_DEMO_KEY[%d] = { %s };" % (
            len(KEY), ", ".join("0x%02x" % k for k in KEY)), file=f)
        print("struct MarbleDemoTick { uint16_t x, y; uint8_t state; uint16_t cursor, rng; uint32_t crc; };", file=f)
        print("struct MarbleDemoBanner { uint16_t tick; uint8_t banner; };", file=f)
        for race in sorted(runs):
            rng, ticks, banners = runs[race]
            print("static const uint16_t MARBLE_DEMO%d_RNG = 0x%04x;" % (race, rng), file=f)
            print("static const MarbleDemoTick MARBLE_DEMO%d_TICKS[%d] = {" % (race, len(ticks)), file=f)
            for fr, r in ticks:
                key = bytes(mh.rb(r, P + k) for k in KEY)
                print("  { 0x%04x, 0x%04x, %d, 0x%04x, 0x%04x, 0x%08xu }," % (
                    mh.rw(r, P + 0xc), mh.rw(r, P + 0x10), mh.rb(r, P + 0x1a),
                    mh.rl(r, ms.WAYPOINT) & 0xffff, mh.rw(r, RNG), crc32(key)), file=f)
            print("};", file=f)
            print("static const MarbleDemoBanner MARBLE_DEMO%d_BANNERS[%d] = { %s };" % (
                race, len(banners), ", ".join("{ %d, %d }" % b for b in banners)), file=f)
    print("wrote", out, {r: len(v[1]) for r, v in runs.items()})


def main(argv):
    if len(argv) >= 2 and argv[0] == "check":
        sys.exit(0 if all([check_trace(p) for p in argv[1:]]) else 1)
    if len(argv) >= 3 and argv[0] == "fwrite":
        write_fixture(argv[1], argv[2:])
        return
    print(__doc__)
    sys.exit(2)


if __name__ == "__main__":
    main(sys.argv[1:])
