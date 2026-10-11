#!/usr/bin/env python3
"""Marble physics host test fixture from MAME traces (tools/mame_trace.lua).

A segment starts at one trace snapshot and replays the player update with marble_sim.py
using only the initial work RAM blocks below: no level object slots (Phase 4), no per tick
copies of dynamic terrain codes or enemy scratch words. Positions must equal the trace every
tick; the segment ends at the death the sim detects (its frame gap in the trace confirms it).

  make_fixture.py search TRACE RACE [MIN]   list reproducible segments ending in a death
  make_fixture.py write OUT RACE:TRACE:FRAME:TICKS ...
                                            write the C header (marble_trace.h)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import marble_sim as ms

P = ms.P
TICK_FRAMES = 2       # logic tick = 2 frames during a race
SLOTS = (0xa9c, 0xa9c + 25 * 0x56)
# initial RAM blocks (68010 address, length): object, race words, scratch globals, dynamic
# terrain codes, vertex heights (MARBLE.md "Marble physics"). Only blocks the update reads
# before writing are emitted.
BLOCKS = ((P, 0xe2), (0x400390, 6), (0x400660, 0x48), (ms.DYN, 0x84), (0x401c28, 0x20))
# object bytes that must equal the trace every tick (more than the fixture stores, so hidden
# state such as +0x36 cannot drift): velocity, position, state, zone, ground/edge flags,
# dizzy counters, surface code
CHECK = tuple(range(0x18)) + (0x1a, 0x1b, 0x36, 0x37, 0x56, 0x57, 0x58)


class RecMem(ms.Mem):
    """Mem that records work RAM bytes read before the run writes them."""
    def __init__(self, terrain, ram):
        super().__init__(terrain, ram)
        self.written = set()
        self.first_read = set()
    def rb(self, a):
        o = (a & 0xffffff) - 0x400000
        if 0 <= o < 0x2000 and o not in self.written:
            self.first_read.add(o)
        return super().rb(a)
    def wb(self, a, v):
        self.written.add((a & 0xffffff) - 0x400000)
        super().wb(a, v)


def run(t, trace, i, m, limit=10 ** 6):
    """Replay from snapshot i without overrides. Returns (ticks, end, stats)."""
    ticks = []
    stats = {"air": 0, "teeter": 0, "land": 0, "wall": 0, "side": 0, "left": 0,
             "vx<0": 0, "vy<0": 0}
    for (fr, r, _), (fr2, r2, _) in zip(trace[i:], trace[i + 1:]):
        if len(ticks) == limit:
            return ticks, "limit", stats
        c6, c7 = ms.tb(r2, P + 0xc6), ms.tb(r2, P + 0xc7)
        sx0 = ms.s16(m.rw(P + 0x10) - m.rw(P + 0xc))
        m.events = []; m.sounds = []
        ms.tick(m, c6, c7)
        pos = (m.sl(P + 0xc), m.sl(P + 0x10), m.sl(P + 0x14))
        state = m.rb(P + 0x1a)
        ticks.append((ms.s8(c6), ms.s8(c7)) + pos + (state,))
        if any(e in ms.DEATH for e in m.events):
            return ticks, ("death %s, trace gap %d frames" % (m.events[-1], fr2 - fr)
                           if fr2 - fr != TICK_FRAMES else "unconfirmed death"), stats
        if fr2 - fr != TICK_FRAMES or any(m.rb(P + o) != ms.tb(r2, P + o) for o in CHECK):
            ticks.pop()
            return ticks, "diverge at frame %d" % fr, stats
        stats["air"] += m.rb(P + 0x36) == 2
        stats["teeter"] += m.rb(P + 0x36) == 1
        stats["land"] += any(isinstance(e, tuple) and e[0] == "land" for e in m.events)
        stats["wall"] += 0x42 in m.sounds
        stats["side"] += "side" in m.events
        stats["left"] += ms.s16(m.rw(P + 0x10) - m.rw(P + 0xc)) < sx0
        stats["vx<0"] += m.sl(P) < 0
        stats["vy<0"] += m.sl(P + 4) < 0
    return ticks, "trace end", stats


def snapshot_ram(r):
    ram = bytearray(r)
    ram[SLOTS[0]:SLOTS[1]] = bytes(SLOTS[1] - SLOTS[0])
    return ram


def segment(t, trace, i, limit=10 ** 6):
    """Run from snapshot i, then again from only the RAM blocks it read: both must agree."""
    m = RecMem(t, snapshot_ram(trace[i][1]))
    ticks, end, stats = run(t, trace, i, m, limit)
    reads = {o for o in m.first_read if not SLOTS[0] <= o < SLOTS[1]}
    blocks = []
    for addr, n in BLOCKS:
        o = addr - 0x400000
        if any(o <= k < o + n for k in reads):
            blocks.append((addr, bytes(trace[i][1][o:o + n])))
            reads -= set(range(o, o + n))
    if reads:
        raise SystemExit("RAM read outside the fixture blocks: %s"
                         % sorted(hex(0x400000 + k) for k in reads))
    seg = {"race": t.race, "frame": trace[i][0], "ram": blocks, "ticks": ticks}
    if ms.replay_fixture(seg, ms.fixture_mem(seg, t)) is not None:
        raise SystemExit("segment at frame %d does not replay from its RAM blocks" % seg["frame"])
    return seg, end, stats


def cmd_search(path, race, need):
    t = ms.Terrain(race)
    trace = ms.load_trace(path)
    i = 0
    while i < len(trace) - 1:
        if not ms.live(trace[i][1]):
            i += 1; continue
        seg, end, stats = segment(t, trace, i)
        n = len(seg["ticks"])
        if n >= need and end.startswith("death "):
            print("frame %d: %d ticks, %s, %s" % (seg["frame"], n, end, stats))
            i += n
        else:
            i += 10


def c_bytes(b):
    return ",\n".join("  " + ", ".join("0x%02x" % v for v in b[k:k + 16])
                      for k in range(0, len(b), 16))


def c_int(v):
    return "%d" % v if v < 0 else "0x%08x" % v


def cmd_write(out, specs):
    segs = []
    for spec in specs:
        race, path, frame, n = spec.split(":")
        race, frame, n = int(race), int(frame), int(n)
        trace = ms.load_trace(path)
        i = next(k for k, x in enumerate(trace) if x[0] == frame)
        seg, end, stats = segment(ms.Terrain(race), trace, i, n)
        if len(seg["ticks"]) != n:
            raise SystemExit("race %d frame %d: only %d ticks (%s)" % (race, frame, len(seg["ticks"]), end))
        print("race %d frame %d: %d ticks, %s, %s, RAM %s" % (race, frame, n, end, stats,
              ", ".join("%06x+%d" % (a, len(b)) for a, b in seg["ram"])))
        segs.append(seg)
    with open(out, "w") as f:
        w = lambda s="": print(s, file=f)
        w("// Generated by romconv/internal/marble/tools/make_fixture.py; do not edit.")
        w("// Marble physics host test: MAME trace segments (tools/mame_trace.lua) the reference")
        w("// model tools/marble_sim.py reproduces without level object slots. Derived numbers")
        w("// only: trackball deltas, positions, initial work RAM bytes. Replay check:")
        w("//   python3 romconv/internal/marble/tools/marble_sim.py fixture <this file>")
        w("#ifndef MARBLE_TRACE_H\n#define MARBLE_TRACE_H\n#include <stdint.h>\n")
        w("// Work RAM 0x400000-0x401fff is 0 except these blocks (68010 address, big-endian).")
        w("struct MarbleTraceRam { uint32_t addr; uint16_t len; const uint8_t *bytes; };")
        w("// One logic tick: tick(c6, c7), then player 0x400018 has x, y, z (16.16, +0x0c/10/14)")
        w("// and state (+0x1a). The last tick of a segment is the death (state 4 or 2).")
        w("struct MarbleTraceTick { int8_t c6, c7; int32_t x, y, z; uint8_t state; };")
        w("struct MarbleTraceSegment {")
        w("  uint8_t race; uint32_t frame;  // race 0-5, MAME frame of the first snapshot")
        w("  uint8_t nram; const MarbleTraceRam *ram; uint16_t nticks; const MarbleTraceTick *ticks;")
        w("};\n")
        for k, seg in enumerate(segs):
            p = "marble_trace_s%d" % k
            for j, (addr, b) in enumerate(seg["ram"]):
                w("static const uint8_t %s_ram%d[] = {\n%s\n};" % (p, j, c_bytes(b)))
            w("static const MarbleTraceRam %s_ram[] = {" % p)
            for j, (addr, b) in enumerate(seg["ram"]):
                w("  { 0x%06x, %d, %s_ram%d }," % (addr, len(b), p, j))
            w("};")
            w("static const MarbleTraceTick %s_ticks[] = {" % p)
            for c6, c7, x, y, z, st in seg["ticks"]:
                w("  { %d, %d, %s, %s, %s, %d }," % (c6, c7, c_int(x), c_int(y), c_int(z), st))
            w("};\n")
        w("static const MarbleTraceSegment marble_trace_segments[] = {")
        for k, seg in enumerate(segs):
            p = "marble_trace_s%d" % k
            w("  { %d, %d, %d, %s_ram, %d, %s_ticks }," % (seg["race"], seg["frame"],
              len(seg["ram"]), p, len(seg["ticks"]), p))
        w("};")
        w("static const uint8_t MARBLE_TRACE_SEGMENTS = %d;\n#endif" % len(segs))


def main(argv):
    if len(argv) >= 4 and argv[1] == "search":
        cmd_search(argv[2], int(argv[3]), int(argv[4]) if len(argv) > 4 else 300)
        return 0
    if len(argv) >= 4 and argv[1] == "write":
        cmd_write(argv[2], argv[3:])
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
