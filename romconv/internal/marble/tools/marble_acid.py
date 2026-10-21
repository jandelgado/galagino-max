#!/usr/bin/env python3
"""Marble Madness acid model (Phase 4b-3): green puddles 0x401482 (7 x 0x42, draw type 0x0e).
1:1 ports named by ROM address from Marble Love (string-range-dispatch-17346.ts,
dispatch-strings-17230.ts, string-step-1725a.ts, entity-waypoint-step-1d1ec.ts, sub-1d242.ts,
sprite-coords.ts 0x1778e, string-viewport-hit-175c8.ts, string-target-step-176d2.ts,
refresh-frame-10fce.ts JT[9]) on the work RAM image of marble_sim.Mem. Facts: MARBLE.md
"Enemies".

Fields: v +0/+4 (+-8 px per anim loop, one axis), pos +0xc/+0x10/+0x14, +0x18 live, +0x19
index, +0x1a state (always 0), +0x1b zone, +0x24 tick, +0x25 ticks per frame, +0x28 screen (x
word, y word), +0x2c path cursor, +0x30 path (4 byte records cell x, y, next record, pad;
0xff end), +0x34/+0x36 band, +0x3a frame cursor, +0x3e anim list.

  marble_acid.py spawncheck TRACE RACE [mid]  model spawner 0x17346 over the trace camera ==
                                         the acid slots of the next snapshot (mid: 0x121b8
                                         entry snapshots)
  marble_acid.py acid TRACE RACE [mid]   per tick from each snapshot: 0x17230 == next
                                         snapshot's acid records
  marble_acid.py acids TRACE RACE FROM:TO...  free run main_tick (tick start traces): player,
                                         enemies, draw list, acid MOs equal to MAME
  marble_acid.py acfixture FILE          replay test_marble_acid/marble_acid_trace.h
  marble_acid.py sweep                   race sweep (all races, all modelled enemies)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from devrom import ROM, SIM_ROM, md  # noqa: F401
from romconv.games.marble.sim import marble_acid as _model  # noqa: F401
from romconv.games.marble.sim.marble_acid import *  # noqa: F401,F403
import marble_sim as ms
from marble_sim import s8, s16, s32, rom_l, PROG



ACID_SIZE = 0x42


# ---------------------------------------------------------------- trace checks

def cmd_spawncheck(trace, race, mid=False):
    """Model spawner over consecutive snapshots' cameras vs the next snapshot's live slots
    and paths. mid: snapshots at the player's 0x121b8 entry (not tick start), the tick's
    rng(0x100) 0x118a4 is still due before the next spawn."""
    n = bad = ev = 0; t = ms.Terrain(race)
    for (_, r, _), (_, r2, _) in zip(trace, trace[1:]):
        m = ms.Mem(t, r)
        old = m.rw(0x40097e); new = ms.tw(r2, 0x40097e) & 0xffff
        top = m.sw(m.t.desc + 0x10)
        a = s16(old - top) >> 4; b = s16(new - top) >> 4
        if mid: ms.rng_13a98(m, 0x100)
        if (a & 0xff) != (b & 0xff): spawn_17346(m, a, b); ev += len(m.events)
        want = [(ms.tb(r2, s + 0x18) != 0, ms.tl(r2, s + 0x30) if ms.tb(r2, s + 0x18) else 0)
                for s in ACIDS]
        got = [(m.rb(s + 0x18) != 0, m.sl(s + 0x30) if m.rb(s + 0x18) else 0) for s in ACIDS]
        n += 1
        if want != got:
            bad += 1
            if bad <= 5: print("differ", n, want, got)
    print("spawncheck", n, "steps", ev, "spawns/removals", bad, "differ")
    return bad == 0


FIELDS = [(0, 4, "vx"), (4, 4, "vy"), (0xc, 4, "x"), (0x10, 4, "y"), (0x14, 4, "z"),
          (0x18, 1, "live"), (0x1a, 2, "state"), (0x24, 2, "tick"), (0x28, 4, "screen"),
          (0x2c, 4, "cursor"), (0x30, 4, "path"), (0x34, 4, "band"), (0x3a, 8, "frame")]


def cmd_acid(trace, race, mid=False):
    """Snapshot pairs 2 frames apart (one tick), same band: 0x17230 on the first snapshot
    == the second snapshot's acid records (acid live in both, not hit in the tick)."""
    t = ms.Terrain(race)
    n = bad = skip = 0; diffs = {}
    for (fr, r, _), (fr2, r2, _) in zip(trace, trace[1:]):
        if fr2 - fr != 2: continue
        live = [s for s in ACIDS if ms.tb(r, s + 0x18) and ms.tb(r2, s + 0x18)]
        if not live: continue
        m = ms.Mem(t, r)
        top = m.sw(m.t.desc + 0x10)
        if s16(m.rw(0x40097e) - top) >> 4 != ms.tw(r2, 0x40097e) - top >> 4 or \
                any(ms.tb(r2, s + 0x25) == HIT_TICKS for s in live):
            skip += 1; continue
        if not mid: m.wl(0x40097c, ms.tl(r2, 0x40097c) & 0xffffffff)   # after the camera store
        update_17230(m)
        n += 1; ok = True
        for s in live:
            for off, size, name in FIELDS:
                a = s - 0x400000 + off
                if m.ram[a:a + size] != r2[a:a + size]:
                    ok = False; diffs[name] = diffs.get(name, 0) + 1
                    if bad < 8:
                        print("tick", fr, hex(s), name, m.ram[a:a + size].hex(), r2[a:a + size].hex())
        if not ok: bad += 1
    print("acid", n, "ticks", bad, "differ", skip, "skipped", diffs)
    return bad == 0


def acid_diff(m, r2, v2):
    """Player, steelies, slinkies, the draw list (marble_slinky.slinky_diff), all seven acid
    records, their MOs."""
    import marble_slinky
    out = marble_slinky.slinky_diff(m, r2, v2)
    for i, a in enumerate(ACIDS):
        o = a - 0x400000
        out += ["a%d+%02x %02x/%02x" % (i, k, m.ram[o + k], r2[o + k])
                for k in range(ACID_SIZE) if m.ram[o + k] != r2[o + k]]
        if acid_mos(m, i) != marble_slinky.ref_mos(r2, v2, TYPE, i): out.append("acid %d MOs" % i)
    return out


def segment(trace, race, start, end):
    """Free run from the first live snapshot after start to end."""
    import marble_enemy, marble_mo
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
        ev |= {e[0] for e in m.events if isinstance(e, tuple) and str(e[0]).startswith("acid")}
        d = acid_diff(m, r2, v2)
        if d:
            print("acid race %d: tick %d (frame %d) differs: %s %s"
                  % (race, n, fr2, "; ".join(d[:12]), [e for e in m.events if e != "slot"][:8]))
            return False
    print("acid race %d: frames %d-%d, %d ticks equal, player ticks per state %s, events %s"
          % (race, run[0][0], run[-1][0], n, states, sorted(ev)))
    return True


def cmd_acids(trace, race, segs):
    ok = True
    for seg in segs:
        f = seg.split(":")
        ok &= segment(trace, race, int(f[0]), int(f[1]))
    return ok


def replay_acfixture(seg, m):
    """Run every acid fixture tick on m (main_tick + 0x26f3e sort); index of the first tick
    that differs, or None."""
    import marble_enemy, marble_mo
    marble_enemy.setup(m)
    for k, (c6, c7, cam, frames, want) in enumerate(seg["ticks"]):
        ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
        marble_mo.boxes_and_sort(m)
        if tick_state(m) != want: return k
    return None


def cmd_acfixture(path):
    ok = True
    for seg in load_acfixture(path):
        bad = replay_acfixture(seg, ms.fixture_mem(seg, ms.Terrain(seg["race"])))
        print("acid fixture race %d frame %d: %d ticks, %s" % (seg["race"], seg["frame"],
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
    if cmd == "spawncheck":
        return 0 if cmd_spawncheck(ms.load_trace(argv[2]), int(argv[3]), argv[4:] == ["mid"]) else 1
    if cmd == "sweep": return 0 if cmd_sweep() else 1
    if cmd == "acfixture": return 0 if cmd_acfixture(argv[2]) else 1
    if cmd == "acids":
        return 0 if cmd_acids(ms.load_trace(argv[2]), int(argv[3]), argv[4:]) else 1
    if cmd == "acid": return 0 if cmd_acid(ms.load_trace(argv[2]), int(argv[3]), argv[4:] == ["mid"]) else 1
    print(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
