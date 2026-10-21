#!/usr/bin/env python3
"""Marble Madness silly race minis (Phase 4b-4b): 0x401890 (9 x 0x28, draw types 7/8/9:
small steelie, slinky, acid; bonus targets). 1:1 ports named by ROM address from Marble Love
(scroll-range-144e4.ts mode 4, array-9-init-and-dispatch-18ffa.ts,
array-9-clear-and-dispatch.ts 0x190ee, refresh-helper-1912c.ts, object-type-dispatch-194ba.ts,
state-sub-1953e.ts, state-sub-1960e.ts, sub-19692.ts, state-sub-198bc.ts, sub-19976.ts,
sub-1937c.ts, helper-1924e.ts, sprite-coords.ts 0x199d6) on the work RAM image of
marble_sim.Mem; 0x1973c (kind 1, TS calls 0x198bc directly) from the disassembly (user ruling
2026-10-12), 0x1953e from the disassembly too (the TS writes one list per type; MAME picks
one by direction; user ruling 2026-10-12). Facts: MARBLE.md "Enemies".

Fields: v +0/+4 (set by the direction step 0x19976, applied at an anim list end), pos
+0xc/+0x10/+0x14 (z 0x3f6e), +0x18 live, +0x19 index, +0x1a kind (0 wander, 1 a player on
the 0x3f6e floor of zone 1: steelies flee / the others follow, 2 collected), +0x1b steps,
+0x1c anim cursor, +0x20 screen (x, y words), +0x24 tick, +0x25 type 7/8/9, +0x26 direction
(16ths; 0x10 stuck).

  marble_mini.py minis TRACE RACE FROM:TO...  free run main_tick (tick start traces): player,
                                         minis, birds, draw list, RNG, mini MOs equal to MAME
  marble_mini.py mnfixture FILE          replay test_marble_minis/marble_minis_trace.h
  marble_mini.py sweep                   race sweep (all races, all modelled enemies)
"""
import os
import sys


from dev.marble.devrom import ROM, SIM_ROM, md  # noqa: F401
from romconv.games.marble.sim import marble_mini as _model  # noqa: F401
from romconv.games.marble.sim.marble_mini import *  # noqa: F401,F403
from dev.marble import marble_sim as ms
from dev.marble.marble_sim import s8, s16, s32, rom_l, rom_w, PROG



MINI_SIZE = 0x28


def mini_diff(m, r2, v2):
    """bird_diff (player, enemies, birds, draw list, RNG via the player diff), the nine mini
    records, their MOs."""
    from dev.marble import marble_bird, marble_slinky
    out = marble_bird.bird_diff(m, r2, v2)
    for i, a in enumerate(MINIS):
        o = a - 0x400000
        out += ["m%d+%02x %02x/%02x" % (i, k, m.ram[o + k], r2[o + k])
                for k in range(MINI_SIZE) if m.ram[o + k] != r2[o + k]]
        t = m.rb(a + 0x25)
        if t and mini_mos(m, i) != marble_slinky.ref_mos(r2, v2, t, i): out.append("mini %d MOs" % i)
    return out


def segment(trace, race, start, end):
    """Free run from the first live snapshot after start to end."""
    from dev.marble import marble_enemy, marble_mo
    t = ms.Terrain(race)
    i0 = next(k for k, x in enumerate(trace) if x[0] > start and ms.live_race(x[1]))
    m = marble_enemy.setup(ms.Mem(t, trace[i0][1]))
    run = [x for x in trace[i0:] if x[0] <= end]
    n = 0; states = {}; ev = set()
    for (fr, r, v), (fr2, r2, v2) in zip(run, run[1:]):
        m.events = []; m.sounds = []
        ms.main_tick(m, *ms.death_inputs(r, r2))
        marble_mo.boxes_and_sort(m)
        n += 1
        k = m.rb(ms.P + 0x1a); states[k] = states.get(k, 0) + 1
        ev |= {e[0] for e in m.events if isinstance(e, tuple) and str(e[0])[:4] in ("mini", "bird", "tube")}
        d = mini_diff(m, r2, v2)
        if d:
            print("minis race %d: tick %d (frame %d) differs: %s %s"
                  % (race, n, fr2, "; ".join(d[:12]), [e for e in m.events if e != "slot"][:8]))
            return False
    print("minis race %d: frames %d-%d, %d ticks equal, player ticks per state %s, events %s"
          % (race, run[0][0], run[-1][0], n, states, sorted(ev)))
    return True


def cmd_minis(trace, race, segs):
    ok = True
    for seg in segs:
        f = seg.split(":")
        ok &= segment(trace, race, int(f[0]), int(f[1]))
    return ok


def replay_mnfixture(seg, m):
    """Run every mini fixture tick on m (main_tick + 0x26f3e sort); index of the first tick
    that differs, or None."""
    from dev.marble import marble_enemy, marble_mo
    marble_enemy.setup(m)
    for k, (c6, c7, cam, frames, want) in enumerate(seg["ticks"]):
        ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
        marble_mo.boxes_and_sort(m)
        if tick_state(m) != want: return k
    return None


def cmd_mnfixture(path):
    ok = True
    for seg in load_mnfixture(path):
        bad = replay_mnfixture(seg, ms.fixture_mem(seg, ms.Terrain(seg["race"])))
        print("mini fixture race %d frame %d: %d ticks, %s" % (seg["race"], seg["frame"],
              len(seg["ticks"]), "exact" if bad is None else "differs at tick %d" % bad))
        ok &= bad is None
    return ok


def cmd_sweep():
    from dev.marble import marble_enemy
    for race in range(6):
        m = marble_enemy.setup(ms.Mem(ms.Terrain(race), bytes(0x2000)))
        out, sp, cl = sweep(m, race)
        print("sweep race %d: %d ticks, %d spawns, %d clears, longest list %d" % (
            race, len(out), sp, cl, max(len(o[1]) for o in out)))
    return True


def main(argv):
    cmd = argv[1]
    if cmd == "sweep": return 0 if cmd_sweep() else 1
    if cmd == "mnfixture": return 0 if cmd_mnfixture(argv[2]) else 1
    if cmd == "minis":
        return 0 if cmd_minis(ms.load_trace(argv[2]), int(argv[3]), argv[4:]) else 1
    print(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
