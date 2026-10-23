#!/usr/bin/env python3
"""Marble Madness slinky model (Phase 4b-2): green worms 0x401302 (4 x 0x60, draw type 4).
1:1 ports named by ROM address from Marble Love (state-sub-14c46.ts, sub-14966.ts,
helper-15148.ts, state-dispatch-15460.ts, state-sub-15670.ts, nearest-neighbor.ts,
sprite-coords-jsr-150d0.ts, script-slot-bbox-test-14e92.ts) on the work RAM image of
marble_sim.Mem. Facts: MARBLE.md "Enemies".

Fields: v +0/+4, pos +0xc/+0x10/+0x14, +0x18 live, +0x19 index, +0x1a state, +0x1b zone,
+0x1c/+0x20 marble velocity at the catch, +0x24 tick, +0x25 ticks per frame, +0x26 frame
step, +0x27, +0x28 screen (x word, y word), +0x2c masks (5 x 3 words), +0x4a next waypoint,
+0x4e waypoint list (4 byte records x, y, next, pad), +0x52/+0x54 band, +0x56 prey (index
into 0x1eff6), +0x58 frame cursor, +0x5c anim list.

States: 0 walk the waypoints, 1 chase the prey, 2 pushed by the marble, 3 back to the path,
4 stopped (-> 1), 5 ate the marble (-> 3), 6 lost the prey (-> 3).

0x14a28-0x14bba (states 1/5/6, not in Marble Love) is ported from the disassembly (user
ruling 2026-10-12, this block only).

  marble_slinky.py spawncheck TRACE RACE   model spawner 0x14c46 over the trace camera ==
                                           the slinky slots of the next snapshot
  marble_slinky.py slinky TRACE RACE       per tick from each snapshot: 0x1493c == next
                                           snapshot's slinky records (states 0/2/3/4)
"""
import os
import sys


from dev.marble.devrom import ROM, SIM_ROM, md  # noqa: F401
from romconv.games.marble.sim import marble_slinky as _model  # noqa: F401
from romconv.games.marble.sim.marble_slinky import *  # noqa: F401,F403
from dev.marble import marble_sim as ms
from dev.marble.marble_sim import s8, s16, s32, rom_l, PROG



SLINKY_SIZE = 0x60


# ---------------------------------------------------------------- trace checks

def cmd_spawncheck(trace, race):
    """Model spawner over consecutive snapshots' cameras vs the next snapshot's live slots
    and waypoint lists."""
    n = bad = ev = 0; t = ms.Terrain(race)
    for (_, r, _), (_, r2, _) in zip(trace, trace[1:]):
        m = ms.Mem(t, r)
        old = m.rw(0x40097e); new = ms.tw(r2, 0x40097e) & 0xffff
        top = m.sw(m.t.desc + 0x10)
        a = s16(old - top) >> 4; b = s16(new - top) >> 4
        if (a & 0xff) != (b & 0xff): spawn_14c46(m, a, b); ev += len(m.events)
        want = [(ms.tb(r2, s + 0x18) != 0, ms.tl(r2, s + 0x4e) if ms.tb(r2, s + 0x18) else 0)
                for s in SLINKIES]
        got = [(m.rb(s + 0x18) != 0, m.sl(s + 0x4e) if m.rb(s + 0x18) else 0) for s in SLINKIES]
        n += 1
        if want != got:
            bad += 1
            if bad <= 5: print("differ", n, want, got)
    print("spawncheck", n, "steps", ev, "spawns/removals", bad, "differ")
    return bad == 0


FIELDS = [(0, 4, "vx"), (4, 4, "vy"), (0xc, 4, "x"), (0x10, 4, "y"), (0x14, 4, "z"),
          (0x18, 1, "live"), (0x1a, 1, "state"), (0x1c, 8, "catch v"), (0x24, 4, "anim"),
          (0x28, 4, "screen"), (0x2c, 30, "masks"), (0x4a, 4, "next"), (0x56, 2, "prey"),
          (0x58, 8, "frame")]


def cmd_slinky(trace, race):
    """Snapshot pairs 2 frames apart (one tick), player rolling: 0x1493c on the first
    snapshot == the second snapshot's slinky records, slinkies in states 0/2/3/4 only."""
    t = ms.Terrain(race)
    n = bad = skip = 0; diffs = {}
    for (fr, r, _), (fr2, r2, _) in zip(trace, trace[1:]):
        if fr2 - fr != 2: continue
        live = [s for s in SLINKIES if ms.tb(r, s + 0x18) and ms.tb(r2, s + 0x18)]
        if not live: continue
        if any(ms.tb(r, s + 0x1a) in (1, 5, 6) or ms.tb(r2, s + 0x1a) in (1, 5, 6) for s in live) \
                or ms.tw(r, 0x40097e) != ms.tw(r2, 0x40097e):
            skip += 1; continue
        m = ms.Mem(t, r); m.sprites = True
        update_1493c(m)
        n += 1; ok = True
        for s in live:
            for off, size, name in FIELDS:
                a = s - 0x400000 + off
                if m.ram[a:a + size] != r2[a:a + size]:
                    ok = False; diffs[name] = diffs.get(name, 0) + 1
                    if bad < 8:
                        print("tick", fr, hex(s), name, m.ram[a:a + size].hex(), r2[a:a + size].hex(),
                              "state", ms.tb(r, s + 0x1a))
        if not ok: bad += 1
    print("slinky", n, "ticks", bad, "differ", skip, "skipped", diffs)
    return bad == 0


def ref_mos(r, v, typ, i):
    """MO words of object (typ, i) in the MO bank the snapshot shows built."""
    from dev.marble import marble_mo
    words, _ = marble_mo.mo_ram(r, v)
    k = 0
    for _, t, idx, mos in marble_mo.mo_list_26f3e(marble_mo.Ram(r)):
        if t == typ and idx == i: return words[k:k + len(mos)]
        k += len(mos)
    return []


def slinky_diff(m, r2, v2):
    """Player and steelies (marble_enemy.steelie_diff), the whole draw list in order (acid
    modelled since 4b-3), all four slinky records, their MOs."""
    from dev.marble import marble_enemy
    out = [d for d in marble_enemy.steelie_diff(m, r2, v2) if not d.startswith("draw list")]
    want = marble_enemy.drawn(ms.Mem(m.t, r2)); got = marble_enemy.drawn(m)
    if got != want: out.append("draw list %s/%s" % (got, want))
    for i, a in enumerate(SLINKIES):
        o = a - 0x400000
        out += ["l%d+%02x %02x/%02x" % (i, k, m.ram[o + k], r2[o + k])
                for k in range(SLINKY_SIZE) if m.ram[o + k] != r2[o + k]]
        if slinky_mos(m, i) != ref_mos(r2, v2, 4, i): out.append("slinky %d MOs" % i)
    return out


def segment(trace, race, start, end):
    """Free run from the first live snapshot after start to end."""
    from dev.marble import marble_mo
    t = ms.Terrain(race)
    i0 = next(k for k, x in enumerate(trace) if x[0] > start and ms.live_race(x[1]))
    m = setup(ms.Mem(t, trace[i0][1]))
    run = [x for x in trace[i0:] if x[0] <= end]
    n = 0; states = {}; ev = set()
    for (fr, r, v), (fr2, r2, v2) in zip(run, run[1:]):
        m.events = []; m.sounds = []
        ms.main_tick(m, *ms.death_inputs(r, r2))
        marble_mo.boxes_and_sort(m)
        n += 1
        for a in SLINKIES:
            if m.rb(a + 0x18): k = m.rb(a + 0x1a); states[k] = states.get(k, 0) + 1
        ev |= {e[0] for e in m.events if isinstance(e, tuple) and str(e[0]).startswith(("slinky", "eaten"))}
        d = slinky_diff(m, r2, v2)
        if d:
            print("slinkies race %d: tick %d (frame %d) differs: %s %s"
                  % (race, n, fr2, "; ".join(d[:12]), [e for e in m.events if e != "slot"][:8]))
            return False
    print("slinkies race %d: frames %d-%d, %d ticks equal, slinky ticks per state %s, events %s"
          % (race, run[0][0], run[-1][0], n, states, sorted(ev)))
    return True


def cmd_slinkies(trace, race, segs):
    ok = True
    for seg in segs:
        f = seg.split(":")
        ok &= segment(trace, race, int(f[0]), int(f[1]))
    return ok


def replay_slfixture(seg, m):
    """Run every slinky fixture tick on m (main_tick + 0x26f3e sort); index of the first tick
    that differs, or None."""
    from dev.marble import marble_mo
    setup(m)
    for k, (c6, c7, cam, frames, want) in enumerate(seg["ticks"]):
        ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
        marble_mo.boxes_and_sort(m)
        if tick_state(m) != want: return k
    return None


def cmd_slfixture(path):
    ok = True
    for seg in load_slfixture(path):
        bad = replay_slfixture(seg, ms.fixture_mem(seg, ms.Terrain(seg["race"])))
        print("slinkies fixture race %d frame %d: %d ticks, %s" % (seg["race"], seg["frame"],
              len(seg["ticks"]), "exact" if bad is None else "differs at tick %d" % bad))
        ok &= bad is None
    return ok


def cmd_sweep():
    for race in range(6):
        m = setup(ms.Mem(ms.Terrain(race), bytes(0x2000)))
        out, sp, rm = sweep(m, race)
        print("sweep race %d: %d ticks, %d spawns, %d removals, most live %d, longest list %d" % (
            race, len(out), sp, rm, max(sum(1 for v in o[0] if v) for o in out),
            max(len(o[1]) for o in out)))
    return True


def main(argv):
    cmd = argv[1]
    if cmd == "spawncheck": return 0 if cmd_spawncheck(ms.load_trace(argv[2]), int(argv[3])) else 1
    if cmd == "sweep": return 0 if cmd_sweep() else 1
    if cmd == "slfixture": return 0 if cmd_slfixture(argv[2]) else 1
    if cmd == "slinkies":
        return 0 if cmd_slinkies(ms.load_trace(argv[2]), int(argv[3]), argv[4:]) else 1
    if cmd == "slinky": return 0 if cmd_slinky(ms.load_trace(argv[2]), int(argv[3])) else 1
    print(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
