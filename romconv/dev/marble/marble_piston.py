#!/usr/bin/env python3
"""Marble Madness aerial race pistons (Phase 4b-5): table 0x401650 (36 x 0x10, draw type
0x29). 1:1 ports named by ROM address from Marble Love (state-sub-186ac.ts,
read-abs-long-1844a.ts, state-sub-1881c.ts, sprite-coords.ts computeSpriteCoords_v4 0x18972,
sprite-derive.ts 0x1bb28) on the work RAM image of marble_sim.Mem; the 0x1881c result (cell
refresh 0x1bab2 in 0x121b8, the TS drops it) from the disassembly 0x123ec-0x12400. Facts:
MARBLE.md "Enemies".

Entry fields: +0 index, +2 timer word (counts down; 0xffff = up, anim running), +4/+5 cell x/y
(bytes), +6 top z word, +8 anim cursor (from 0x21342), +0xc screen (x word, y word).
Globals: 0x400760 armed, 0x400764 timer table of entries 0x18-0x23 (one of 4 variants).

  marble_piston.py pistons TRACE RACE FROM:TO...  free run main_tick (tick start traces):
                                       player, enemies, pistons, draw list, RNG, MOs equal
  marble_piston.py psfixture FILE      replay test_marble_pistons/marble_pistons_trace.h
  marble_piston.py sweep               race sweep (all races, all modelled enemies)
"""
import os
import sys


from dev.marble.devrom import ROM, SIM_ROM, md  # noqa: F401
from romconv.games.marble.sim import marble_piston as _model  # noqa: F401
from romconv.games.marble.sim.marble_piston import *  # noqa: F401,F403
from dev.marble import marble_sim as ms
from dev.marble.marble_sim import s8, s16, s32, rom_l, rom_w, PROG



PISTON_SIZE = 0x10


def piston_diff(m, r2, v2):
    """mini_diff (player, enemies, birds, minis, draw list, RNG via the player diff), the 36
    piston entries, 0x400760-0x400767, their MOs."""
    from dev.marble import marble_mini, marble_slinky
    out = marble_mini.mini_diff(m, r2, v2)
    for i, a in enumerate(PISTONS):
        o = a - 0x400000
        out += ["p%d+%02x %02x/%02x" % (i, k, m.ram[o + k], r2[o + k])
                for k in range(PISTON_SIZE) if m.ram[o + k] != r2[o + k]]
        if m.rw(a + 2) == UP and piston_mos(m, i) != marble_slinky.ref_mos(r2, v2, TYPE, i):
            out.append("piston %d MOs" % i)
    for a in range(ARMED, TIMERS + 4):
        if a in (ARMED + 1, ARMED + 2, ARMED + 3): continue
        o = a - 0x400000
        if m.ram[o] != r2[o]: out.append("%06x %02x/%02x" % (a, m.ram[o], r2[o]))
    return out


def segment(trace, race, start, end):
    """Free run from the first live snapshot after start to end."""
    from dev.marble import marble_enemy, marble_mo
    t = ms.Terrain(race)
    i0 = next(k for k, x in enumerate(trace) if x[0] > start and ms.live_race(x[1]))
    m = marble_enemy.setup(ms.Mem(t, trace[i0][1]))
    run = [x for x in trace[i0:] if x[0] <= end]
    n = 0; states = {}; ev = {}
    for (fr, r, v), (fr2, r2, v2) in zip(run, run[1:]):
        m.events = []; m.sounds = []
        ms.main_tick(m, *ms.death_inputs(r, r2))
        marble_mo.boxes_and_sort(m)
        n += 1
        k = m.rb(ms.P + 0x1a); states[k] = states.get(k, 0) + 1
        for e in m.events:
            if isinstance(e, tuple) and str(e[0])[:6] == "piston": ev[e[0]] = ev.get(e[0], 0) + 1
        d = piston_diff(m, r2, v2)
        if d:
            print("pistons race %d: tick %d (frame %d) differs: %s %s"
                  % (race, n, fr2, "; ".join(d[:12]), [e for e in m.events if e != "slot"][:8]))
            return False
    print("pistons race %d: frames %d-%d, %d ticks equal, player ticks per state %s, events %s"
          % (race, run[0][0], run[-1][0], n, states, sorted(ev.items())))
    return True


def cmd_pistons(trace, race, segs):
    ok = True
    for seg in segs:
        f = seg.split(":")
        ok &= segment(trace, race, int(f[0]), int(f[1]))
    return ok


def replay_psfixture(seg, m):
    """Run every piston fixture tick on m (main_tick + 0x26f3e sort); index of the first tick
    that differs, or None."""
    from dev.marble import marble_enemy, marble_mo
    marble_enemy.setup(m)
    for k, (c6, c7, cam, frames, want) in enumerate(seg["ticks"]):
        ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
        marble_mo.boxes_and_sort(m)
        if fixture_state(tick_state(m)) != want: return k
    return None


def cmd_psfixture(path):
    ok = True
    for seg in load_psfixture(path):
        bad = replay_psfixture(seg, ms.fixture_mem(seg, ms.Terrain(seg["race"])))
        print("piston fixture race %d frame %d: %d ticks, %s" % (seg["race"], seg["frame"],
              len(seg["ticks"]), "exact" if bad is None else "differs at tick %d" % bad))
        ok &= bad is None
    return ok


def cmd_sweep():
    from dev.marble import marble_enemy
    for race in range(6):
        m = marble_enemy.setup(ms.Mem(ms.Terrain(race), bytes(0x2000)))
        out = sweep(m, race)
        print("sweep race %d: %d ticks, armed ticks %d, longest list %d" % (
            race, len(out), sum(1 for o in out if o[0]), max(len(o[1]) for o in out)))
    return True


def main(argv):
    cmd = argv[1]
    if cmd == "sweep": return 0 if cmd_sweep() else 1
    if cmd == "psfixture": return 0 if cmd_psfixture(argv[2]) else 1
    if cmd == "pistons":
        return 0 if cmd_pistons(ms.load_trace(argv[2]), int(argv[3]), argv[4:]) else 1
    print(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
