#!/usr/bin/env python3
"""Marble Madness silly race birds (Phase 4b-4a): 0x4019f8 (10 x 0x38, draw type 0x0f).
1:1 ports named by ROM address from Marble Love (scroll-range-144e4.ts mode 4,
state-sub-19baa.ts, state-sub-19a40.ts, marble-cell-dispatch-19e42.ts,
bbox-hit-test-19d94.ts, refresh-frame-10fce.ts JT[11]) on the work RAM image of
marble_sim.Mem; 0x264aa mode 3 (bird masks) from the disassembly (user ruling 2026-10-12),
in marble_sim.occlusion_264aa. Facts: MARBLE.md "Enemies".

Fields: v +4 (y only: -4 or -2 px per tick while walking), pos +0xc/+0x10/+0x14 (z fixed
0x3fd8 at spawn), +0x18 live, +0x19 index, +0x1a state (0 walk, 1 standing, 2 landed or
carrying the marble), +0x1b anim loops left, +0x1c anim cursor (ROM longs, -1 end), +0x20
screen (x word, y word), +0x24 tick, +0x25 ticks per frame, +0x26 masks (3 x {code, x, y}).

  marble_bird.py birds TRACE RACE FROM:TO...  free run main_tick (tick start traces): player,
                                         enemies, birds, 0x400762, draw list, bird MOs equal
                                         to MAME
  marble_bird.py bdfixture FILE          replay test_marble_birds/marble_birds_trace.h
  marble_bird.py sweep                   race sweep (all races, all modelled enemies)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from devrom import ROM, SIM_ROM, md  # noqa: F401
from romconv.games.marble.sim import marble_bird as _model  # noqa: F401
from romconv.games.marble.sim.marble_bird import *  # noqa: F401,F403
import marble_sim as ms
from marble_sim import s8, s16, s32, rom_l, PROG



BIRD_SIZE = 0x38
MINIS = tuple(0x401890 + 0x28 * k for k in range(9))    # 4b-4b; bird traces need them empty


def bird_diff(m, r2, v2):
    """acid_diff (player, enemies, draw list, acid), all ten bird records, 0x400762, their MOs."""
    import marble_acid, marble_slinky
    out = marble_acid.acid_diff(m, r2, v2)
    for i, a in enumerate(BIRDS):
        o = a - 0x400000
        out += ["b%d+%02x %02x/%02x" % (i, k, m.ram[o + k], r2[o + k])
                for k in range(BIRD_SIZE) if m.ram[o + k] != r2[o + k]]
        if bird_mos(m, i) != marble_slinky.ref_mos(r2, v2, TYPE, i): out.append("bird %d MOs" % i)
    o = SPAWN_ON - 0x400000
    if m.ram[o] != r2[o]: out.append("0x400762 %d/%d" % (m.ram[o], r2[o]))
    return out


def segment(trace, race, start, end):
    """Free run from the first live snapshot after start (no live mini) to end."""
    import marble_enemy, marble_mo
    t = ms.Terrain(race)
    i0 = next(k for k, x in enumerate(trace) if x[0] > start and ms.live_race(x[1]))
    m = marble_enemy.setup(ms.Mem(t, trace[i0][1]))
    run = [x for x in trace[i0:] if x[0] <= end]
    n = 0; states = {}; ev = set()
    for (fr, r, v), (fr2, r2, v2) in zip(run, run[1:]):
        if any(r[a + 0x18 - 0x400000] for a in MINIS):
            print("birds race %d: frame %d has a live mini (4b-4b)" % (race, fr)); return False
        m.events = []; m.sounds = []
        ms.main_tick(m, *ms.death_inputs(r, r2))
        marble_mo.boxes_and_sort(m)
        n += 1
        k = m.rb(ms.P + 0x1a); states[k] = states.get(k, 0) + 1
        ev |= {e[0] for e in m.events if isinstance(e, tuple) and str(e[0]).startswith("bird")}
        d = bird_diff(m, r2, v2)
        if d:
            print("birds race %d: tick %d (frame %d) differs: %s %s"
                  % (race, n, fr2, "; ".join(d[:12]), [e for e in m.events if e != "slot"][:8]))
            return False
    print("birds race %d: frames %d-%d, %d ticks equal, player ticks per state %s, events %s"
          % (race, run[0][0], run[-1][0], n, states, sorted(ev)))
    return True


def cmd_birds(trace, race, segs):
    ok = True
    for seg in segs:
        f = seg.split(":")
        ok &= segment(trace, race, int(f[0]), int(f[1]))
    return ok


def replay_bdfixture(seg, m):
    """Run every bird fixture tick on m (main_tick + 0x26f3e sort); index of the first tick
    that differs, or None."""
    import marble_enemy, marble_mo
    marble_enemy.setup(m)
    for k, (c6, c7, cam, frames, want) in enumerate(seg["ticks"]):
        ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
        marble_mo.boxes_and_sort(m)
        if tick_state(m) != want: return k
    return None


def cmd_bdfixture(path):
    ok = True
    for seg in load_bdfixture(path):
        bad = replay_bdfixture(seg, ms.fixture_mem(seg, ms.Terrain(seg["race"])))
        print("bird fixture race %d frame %d: %d ticks, %s" % (seg["race"], seg["frame"],
              len(seg["ticks"]), "exact" if bad is None else "differs at tick %d" % bad))
        ok &= bad is None
    return ok


def cmd_sweep():
    import marble_enemy
    for race in range(6):
        m = marble_enemy.setup(ms.Mem(ms.Terrain(race), bytes(0x2000)))
        out, sp, rm = sweep(m, race)
        print("sweep race %d: %d ticks, %d spawns, %d removals, most live %d, longest list %d" % (
            race, len(out), sp, rm, max(sum(1 for v in o[0] if v) for o in out),
            max(len(o[1]) for o in out)))
    return True


def main(argv):
    cmd = argv[1]
    if cmd == "sweep": return 0 if cmd_sweep() else 1
    if cmd == "bdfixture": return 0 if cmd_bdfixture(argv[2]) else 1
    if cmd == "birds":
        return 0 if cmd_birds(ms.load_trace(argv[2]), int(argv[3]), argv[4:]) else 1
    print(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
