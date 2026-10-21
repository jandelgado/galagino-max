#!/usr/bin/env python3
"""Marble Madness enemy model (Phase 4b research): steelies 0x4009a4 and the object pair
collision 0x1bc88. 1:1 ports named by ROM address (Marble Love helper-182ba.ts,
sub-158f6.ts, helper-1bc88.ts, state-sub-15e24.ts, state-dispatch-1605c.ts,
nearest-neighbor.ts) on the work RAM image of marble_sim.Mem. Facts: MARBLE.md "Enemies".

  marble_enemy.py steelie TRACE RACE   per tick from each MAME snapshot: player physics
                                       (with 0x1bc88) + 0x158cc steelie update == next
                                       snapshot, steelie records byte for byte
  marble_enemy.py spawn TRACE RACE     enemy spawns/removals of the camera band spawners
                                       0x15a12 / 0x14c46 / 0x17346 seen in a trace
  marble_enemy.py spawncheck TRACE RACE  model spawner 0x15a12 over the trace camera ==
                                       the steelie slots of the next snapshot
  marble_enemy.py steelies TRACE RACE FROM:TO[:OBJ:X:Y:Z] ...
                                       free run of marble_sim.main_tick with steelies from a
                                       tick start snapshot (MARBLE_TICK=start; OBJ hex: the
                                       trace's MARBLE_DROP at FROM): player, both steelie
                                       records, draw list (without slinky/acid), steelie MOs,
                                       RNG equal to MAME every tick
  marble_enemy.py sfixture FILE        replay test_marble_steelies/marble_steelies_trace.h
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from devrom import ROM, SIM_ROM, md  # noqa: F401
from romconv.games.marble.sim import marble_enemy as _model  # noqa: F401
from romconv.games.marble.sim.marble_enemy import *  # noqa: F401,F403
import marble_sim as ms
from marble_sim import s8, s16, s32, rom_l, rom_w, PROG



STEELIE_SIZE = 0x7c


def mem(t, r):
    m = ms.Mem(t, r); m.sprites = True; enable(m)
    return m


def cmd_steelie(trace, race):
    t = ms.Terrain(race)
    n = ok = skip = 0; bad = 0; states = {}; ev = {}
    for (fr, r, _), (fr2, r2, _) in zip(trace, trace[1:]):
        act = [a for a in STEELIES if ms.tb(r, a + 0x18) and ms.tb(r2, a + 0x18)]
        if not act or fr2 - fr != 2 or not ms.live(r): continue
        # spawn or removal between the snapshots: band crossing of the camera
        if any(ms.tl(r, a + 0x72) != ms.tl(r2, a + 0x72) for a in act): skip += 1; continue
        m = mem(t, r)
        ms.physics_121b8(m, P)
        for a in STEELIES: update_158f6(m, a)
        for a in act:
            n += 1
            st = ms.tb(r, a + 0x1a); states[st] = states.get(st, 0) + 1
            for e in m.events:
                k = e[0] if isinstance(e, tuple) else e
                ev[k] = ev.get(k, 0) + 1
            o = a - 0x400000
            got = m.ram[o:o + STEELIE_SIZE]; want = r2[o:o + STEELIE_SIZE]
            if any(isinstance(e, tuple) and e[0] in ("shatter anim", "steelie lost") for e in m.events):
                skip += 1; continue
            if got == want: ok += 1; continue
            bad += 1
            if bad <= 10:
                d = [i for i in range(STEELIE_SIZE) if got[i] != want[i]]
                print("frame %d steelie %06x state %02x->%02x: %d bytes differ at %s %s" % (
                    fr, a, st, want[0x1a], len(d), " ".join("%02x" % i for i in d[:16]), m.events))
    print("steelie race %d: %d steelie ticks, %d equal, %d differ, %d skipped (spawn, shatter, "
          "lost); states %s; events %s" % (race, n, ok, bad, skip,
          {hex(k): v for k, v in sorted(states.items())}, ev))
    return bad == 0


SPAWNERS = (("steelie 0x15a12", 0x4009a4, 0x7c, 2, 0x22706, 0x72),
            ("type 4 0x14c46", 0x401302, 0x60, 4, 0x2257a, 0x4e),
            ("type 0e 0x17346", 0x401482, 0x42, 7, 0x23d4a, 0x30))


def cmd_spawn(trace, race):
    """Spawn/removal events per array, with the camera band (camera - desc +0x10) >> 4."""
    desc = rom_l(0x2be00 + 4 * race); top = s16(rom_w(desc + 0x10))
    for name, base, size, cnt, tab, key in SPAWNERS:
        prev = None; out = []
        for fr, r, _ in trace:
            cur = tuple((ms.tb(r, base + i * size + 0x18), ms.tl(r, base + i * size + key) & 0xffffffff)
                        for i in range(cnt))
            band = (ms.tw(r, 0x40097e) - top) >> 4
            if prev is not None and cur != prev:
                for i, (a, b) in enumerate(zip(prev, cur)):
                    if a != b:
                        out.append("f%d band %d slot %d %s %06x" % (fr, band, i,
                                   "on" if b[0] else "off", b[1] if b[0] else a[1]))
            prev = cur
        print("%s race %d: %d events" % (name, race, len(out)))
        for o in out[:24]: print("   ", o)


def cmd_spawncheck(trace, race):
    """0x15a12 over the trace camera: from each snapshot, the band step to the next snapshot's
    camera gives the steelie slots (+0x18 live, +0x72 node list) of the next snapshot."""
    t = ms.Terrain(race); n = bad = 0
    top = s16(rom_w(rom_l(0x2be00 + 4 * race) + 0x10))
    for (fr, r, _), (fr2, r2, _) in zip(trace, trace[1:]):
        a = (ms.tw(r, 0x40097e) - top) >> 4; b = (ms.tw(r2, 0x40097e) - top) >> 4
        if (a & 0xff) == (b & 0xff) or ms.tw(r, 0x400390) == 1: continue
        m = mem(t, r)
        spawn_15a12(m, a, b)
        n += 1
        for s in STEELIES:
            got = (m.rb(s + 0x18) != 0, m.rl(s + 0x72) if m.rb(s + 0x18) else 0)
            want = (ms.tb(r2, s + 0x18) != 0, ms.tl(r2, s + 0x72) & 0xffffffff if ms.tb(r2, s + 0x18) else 0)
            if got[0] == want[0] and got[1] == want[1]: continue
            bad += 1
            if bad <= 10:
                print("frame %d band %d->%d steelie %06x: got %s want %s" % (fr2, a, b, s, got, want))
    print("spawncheck race %d: %d band steps, %d differ" % (race, n, bad))
    return bad == 0


def steelie_mem(t, ram):
    return setup(ms.Mem(t, ram))


def ref_steelie_mos(r, v, i):
    """MO words of steelie i in the MO bank the snapshot shows built."""
    import marble_mo
    words, _ = marble_mo.mo_ram(r, v)
    k = 0
    for _, typ, idx, mos in marble_mo.mo_list_26f3e(marble_mo.Ram(r)):
        if typ == 2 and idx == i: return words[k:k + len(mos)]
        k += len(mos)
    return []


def steelie_diff(m, r2, v2):
    """death_diff plus both steelie records, the draw list order and the steelie MOs."""
    out = ms.death_diff(m, r2, v2)
    for i, a in enumerate(STEELIES):
        o = a - 0x400000
        out += ["s%d+%02x %02x/%02x" % (i, k, m.ram[o + k], r2[o + k])
                for k in range(STEELIE_SIZE) if m.ram[o + k] != r2[o + k]]
        if steelie_mos(m, i) != ref_steelie_mos(r2, v2, i): out.append("steelie %d MOs" % i)
    later = (4, 0xe)                                 # slinkies, acid: later sub-phases
    want = [e for e in drawn(ms.Mem(m.t, r2)) if e[0] not in later]
    got = [e for e in drawn(m) if e[0] not in later]
    if got != want: out.append("draw list %s/%s" % (got, want))
    return out


def drop(m, obj, x, y, z):
    """mame_trace.lua MARBLE_DROP: obj airborne at pixel (x, y), height z, velocity 0."""
    for o in (0, 4, 8): m.wl(obj + o, 0)
    m.wl(obj + 0xc, x << 16 & 0xffffffff); m.wl(obj + 0x10, y << 16 & 0xffffffff)
    m.wl(obj + 0x14, (z + 0x4000) << 16); m.wl(obj + 0x2a, (z + 0x4000) << 16)
    m.wb(obj + 0x36, 2)


def steelie_segment(trace, race, start, end, drops=()):
    """Free run from the first live snapshot after start; drops (obj, x, y, z) are applied to
    it first (the trace's MARBLE_DROP at frame start)."""
    import marble_mo
    t = ms.Terrain(race)
    i0 = next(k for k, x in enumerate(trace) if x[0] > start and ms.live_race(x[1]))
    m = steelie_mem(t, trace[i0][1])
    for d in drops: drop(m, *d)
    run = [x for x in trace[i0:] if x[0] <= end]
    n = 0; modes = {}
    for (fr, r, v), (fr2, r2, v2) in zip(run, run[1:]):
        m.events = []; m.sounds = []
        ms.main_tick(m, *ms.death_inputs(r, r2))
        marble_mo.boxes_and_sort(m)                  # 0x26f3e head
        n += 1
        for a in STEELIES:
            if m.rb(a + 0x18): k = hex(m.rb(a + 0x1a)); modes[k] = modes.get(k, 0) + 1
        d = steelie_diff(m, r2, v2)
        if d:
            print("steelies race %d: tick %d (frame %d) differs: %s %s"
                  % (race, n, fr2, "; ".join(d[:10]), [e for e in m.events if e != "slot"][:8]))
            return False
    print("steelies race %d: frames %d-%d, %d ticks equal, steelie ticks per mode %s"
          % (race, run[0][0], run[-1][0], n, modes))
    return True


def replay_sfixture(seg, m):
    """Run every steelie fixture tick on m (main_tick + 0x26f3e sort); index of the first
    tick that differs, or None."""
    import marble_mo
    setup(m)
    for k, (c6, c7, cam, frames, want) in enumerate(seg["ticks"]):
        ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
        marble_mo.boxes_and_sort(m)
        if tick_state(m) != want: return k
    return None


def cmd_sfixture(path):
    ok = True
    for seg in load_sfixture(path):
        bad = replay_sfixture(seg, ms.fixture_mem(seg, ms.Terrain(seg["race"])))
        print("steelies fixture race %d frame %d: %d ticks, %s" % (seg["race"], seg["frame"],
              len(seg["ticks"]), "exact" if bad is None else "differs at tick %d" % bad))
        ok &= bad is None
    return ok


def cmd_sweep():
    for race in range(6):
        m = setup(ms.Mem(ms.Terrain(race), bytes(0x2000)))
        out, sp, rm = sweep(m, race)
        print("sweep race %d: %d ticks, %d spawns, %d removals, most live %d" % (
            race, len(out), sp, rm, max(sum(1 for v in o[0] if v) for o in out)))
    return True


def cmd_steelies(trace, race, segs):
    """Free runs of main_tick with steelies from tick start snapshots, segments
    FROM:TO[:OBJ:X:Y:Z] (OBJ hex: a MARBLE_DROP at frame FROM)."""
    ok = True
    for seg in segs:
        f = seg.split(":")
        drops = [(int(f[2], 16), int(f[3]), int(f[4]), int(f[5]))] if len(f) > 2 else []
        ok &= steelie_segment(trace, race, int(f[0]), int(f[1]), drops)
    return ok


def main(argv):
    cmd = argv[1]
    if cmd == "sfixture": return 0 if cmd_sfixture(argv[2]) else 1
    if cmd == "sweep": return 0 if cmd_sweep() else 1
    if cmd == "steelies":
        return 0 if cmd_steelies(ms.load_trace(argv[2]), int(argv[3]), argv[4:]) else 1
    if cmd == "spawncheck": return 0 if cmd_spawncheck(ms.load_trace(argv[2]), int(argv[3])) else 1
    if cmd == "steelie": return 0 if cmd_steelie(ms.load_trace(argv[2]), int(argv[3])) else 1
    if cmd == "spawn": cmd_spawn(ms.load_trace(argv[2]), int(argv[3])); return 0
    print(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
