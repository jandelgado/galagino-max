#!/usr/bin/env python3
"""Marble Madness (MAME `marble`) reference model: terrain from ROM and the player marble update.

Phase 2 of the galagino remake. Every routine is a 1:1 port of the 68010 code named by its
ROM address and works on a 68k style work RAM image (0x400000-0x401fff) so a C++ port can
be checked line by line. Facts and formats: MARBLE.md "Marble physics", "Terrain".

Validation against MAME traces (tools/mame_trace.lua), exit code 1 on failure:
  marble_sim.py terrain TRACE RACE   terrain cells built from ROM == MAME video RAM
  marble_sim.py ticks TRACE RACE     each logic tick from a MAME snapshot, ROM terrain
  marble_sim.py run TRACE RACE [MIN] free run from the first snapshot, ROM terrain;
                                     needs one segment of MIN (300) ticks, 0 px error
  marble_sim.py start TRACE RACE     start_259b4 on a cleared RAM runs like the first snapshot
  marble_sim.py fixture HEADER       replay the host test fixture (tools/make_fixture.py),
                                     no trace needed
  marble_sim.py objfixture HEADER   replay the object host test fixture (tools/make_fixture.py
                                     objects): slots, occlusion masks, no trace needed
  marble_sim.py occl TRACE RACE     projection 0x1b9cc + occlusion masks 0x264aa per tick
  marble_sim.py slots TRACE RACE [MIN]  level object slots + dynamic terrain codes run free
                                     (never copied from the trace), marble physics on them
  marble_sim.py deaths TRACE RACE [FROM:TO ...] [rng]  tick start traces (MARBLE_TICK=start): the
                                     main loop tick with deaths (states 1, 2, 4, respawn) runs
                                     free per segment, player, RNG, slots, player MOs == MAME
  marble_sim.py dfixture HEADER     replay the death host test fixture (make_fixture.py dwrite)
  marble_sim.py tfixture HEADER     replay the tube host test fixture (make_fixture.py twrite)
  marble_sim.py afixture HEADER     replay the aerial host test fixture (make_fixture.py awrite)
  marble_sim.py tbfixture HEADER    replay the time bonus fixture (make_fixture.py tbwrite)
`run` copies slots, dynamic codes and enemy scratch from the trace each tick; `slots` only
the camera and enemy scratch. Divergences at deaths and enemy contacts end a segment and
are reported, any other divergence fails. Enemies are not ported.
"""
import os
import struct
import sys

from devrom import ROM, SIM_ROM, md  # noqa: F401
from romconv.games.marble.sim import marble_sim as _model  # noqa: F401
from romconv.games.marble.sim.marble_sim import *  # noqa: F401,F403


# The model takes its ROM explicitly (marble_sim.SimRom); the tools use the one loaded by devrom.
PROG = SIM_ROM.prog


def rom_w(a): return SIM_ROM.w(a)
def rom_l(a): return SIM_ROM.l(a)


_Terrain, _Rd = Terrain, Rd


def Terrain(race): return _Terrain(SIM_ROM, race)
def Rd(race): return _Rd(SIM_ROM, race)





# attract demo (Marble Love waypoint-list-step-1815a.ts, state-machine-schedule.ts)
WAYPOINTS = 0x1d364      # per demo race (0x400394): long, first waypoint record


def demo_start(m, race):
    """Attract demo race start: the waypoint cursor of the race."""
    m.wl(WAYPOINT, rom_l(WAYPOINTS + 4 * race))


# ---------------------------------------------------------------- MAME trace validation

REC = 8 + 0x2000 + 0x3000
ENEMIES = (0x4009a4, 0x400a20)   # other objects of the collision list 0x24ac6


def load_trace(path):
    d = open(path, "rb").read()
    out = []
    for i in range(0, len(d), REC):
        assert d[i:i + 4] == b"TICK"
        out.append((struct.unpack(">I", d[i + 4:i + 8])[0], d[i + 8:i + 8 + 0x2000],
                    d[i + 8 + 0x2000:i + REC]))
    return out


def tl(r, a): return s32(struct.unpack(">I", r[a - 0x400000:a - 0x3ffffc])[0])
def tw(r, a): return s16(struct.unpack(">H", r[a - 0x400000:a - 0x3ffffe])[0])
def tb(r, a): return r[a - 0x400000]


def live(r):
    """Player rolling (state 0/1), in a race (not the attract demo 0x400390 == 1)."""
    return tb(r, P + 0x1a) in (0, 1) and tl(r, P + 0xc) != 0 and tw(r, 0x400390) != 1


def enemy_near(*rams):
    for r in rams:
        for e in ENEMIES:
            if tb(r, e + 0x18) and abs(tw(r, e + 0xc) - tw(r, P + 0xc)) < 24 \
                    and abs(tw(r, e + 0x10) - tw(r, P + 0x10)) < 24:
                return True
    return False


def oracle(m, r):
    """Phase 4 state taken from the trace: level object slots, dynamic codes, and the wall
    outputs 0x400674-0x400683 that enemy updates leave behind (0x1c676 does not always
    write them, see MARBLE.md)."""
    m.ram[0xa9c:0xa9c + 25 * 0x56] = r[0xa9c:0xa9c + 25 * 0x56]
    m.ram[0x76e:0x86e] = r[0x76e:0x86e]
    if any(tb(r, e + 0x18) for e in ENEMIES):
        m.ram[0x674:0x684] = r[0x674:0x684]


FIELDS = ((0x00, "vx"), (0x04, "vy"), (0x08, "vz"), (0x0c, "x"), (0x10, "y"), (0x14, "z"))
DEATH = ("fell", "broken", "embedded")


def cmd_terrain(trace, race):
    t = Terrain(race)
    _, _, v = next(x for x in trace if tl(x[1], P + 0xc) != 0)
    bad = [off for off, data in t.vram_image().items() if v[off:off + 54] != data]
    print("terrain race %d: %d diagonals, %d pairs, %d differ" % (race, t.maxd, t.maxd // 2, len(bad)))
    return not bad


def unported(m):
    """Contact with a level object slot tag that 0x29cce handles but this port does not."""
    return any(isinstance(e, tuple) and e[0] == "slot" for e in m.events)


def cmd_ticks(trace, race):
    t = Terrain(race)
    n = bad = expl = unp = 0
    for (fr, r, _), (fr2, r2, _) in zip(trace, trace[1:]):
        if not live(r): continue
        m = Mem(t, r)
        tick(m, tb(r2, P + 0xc6), tb(r2, P + 0xc7))
        n += 1
        diff = [name for o, name in FIELDS if m.sl(P + o) != tl(r2, P + o)]
        if not diff: continue
        if any(e in DEATH for e in m.events) or not live(r2) or enemy_near(r, r2):
            expl += 1; continue
        if unported(m):
            unp += 1; continue
        bad += 1
        print("tick %d: %s %s" % (fr, diff, m.events))
    print("ticks race %d: %d ticks, %d match, %d death/enemy, %d unported slot, %d bad"
          % (race, n, n - bad - expl - unp, expl, unp, bad))
    return bad == 0


def cmd_occl(trace, race):
    """Projection 0x1b9cc (+0x1e-+0x29) and occlusion masks 0x264aa (+0x1c, +0x38-+0x55)
    of each logic tick from a MAME snapshot (ticks whose x/y/z match)."""
    t = Terrain(race)
    n = bad = masks = hidden = other = 0
    for (fr, r, _), (fr2, r2, _) in zip(trace, trace[1:]):
        if not live(r) or not live(r2): continue
        m = Mem(t, r); m.sprites = True
        tick(m, tb(r2, P + 0xc6), tb(r2, P + 0xc7))
        if any(m.sl(P + o) != tl(r2, P + o) for o in (0xc, 0x10, 0x14)): continue
        n += 1
        a, b = P - 0x400000, P - 0x400000 + 0x56
        got, ref = m.ram[a + 0x1c:a + 0x56], r2[a + 0x1c:a + 0x56]
        masks += r2[a + 0x38] != 0 or r2[a + 0x39] != 0
        hidden += tb(r2, P + 0x1c) == 0
        if got[:0x0e] == ref[:0x0e] and got[0x1c:] == ref[0x1c:]: continue
        if ref[0x1c] and not ref[0x1c] & 0x40:       # not a mask: respawn sparkle (0x13d38)
            other += 1; continue
        bad += 1
        if bad <= 10:
            ent = lambda b_: [(struct.unpack(">H", b_[0x1c + 6 * i:0x1e + 6 * i])[0],
                               s16(struct.unpack(">H", b_[0x1e + 6 * i:0x20 + 6 * i])[0]),
                               s16(struct.unpack(">H", b_[0x20 + 6 * i:0x22 + 6 * i])[0])) for i in range(5)]
            print("tick %d: sx/sy %s vs %s, 1c %d vs %d\n  got %s\n  ref %s" % (
                fr, struct.unpack(">hh", got[2:6]), struct.unpack(">hh", ref[2:6]), got[0], ref[0],
                ent(got), ent(ref)))
    print("occl race %d: %d ticks, %d with masks, %d hidden, %d other writer, %d differ"
          % (race, n, masks, hidden, other, bad))
    return bad == 0


SLOT_RAM = ((0xa9c, 0xa9c + SLOT_N * SLOT_SIZE), (0x76e, 0x86e), (0x970, 0x97c),
            (0x75c, 0x75f), (0x44a, 0x45e), (0x3a6, 0x3a8))   # state the slot port owns
SLOT_FIELDS = ((0x00, 4, "vx"), (0x04, 4, "vy"), (0x0c, 4, "x"), (0x10, 4, "y"), (0x14, 4, "z"),
               (0x18, 1, "active"), (0x1a, 1, "state"), (0x1b, 1, "zone"), (0x1c, 2, "timer"),
               (0x1e, 1, "mode"), (0x1f, 1, "tag"), (0x20, 4, "counters"), (0x24, 2, "loops"),
               (0x26, 4, "col/row"), (0x2a, 8, "loop pcs"), (0x32, 4, "saved pc"),
               (0x36, 4, "pc"), (0x3a, 4, "script"), (0x3e, 4, "anim"), (0x42, 4, "MO frame"),
               (0x46, 8, "anim bases"), (0x4e, 4, "screen"), (0x52, 4, "bands"))


def slot_diff(m, r):
    """Differences between the simulated slot state and a MAME snapshot."""
    out = []
    for i in range(SLOT_N):
        o = 0xa9c + i * SLOT_SIZE
        if m.ram[o + 0x18] == 0 and r[o + 0x18] == 0: continue    # free slots keep junk
        for off, n, name in SLOT_FIELDS:
            if m.ram[o + off:o + off + n] != r[o + off:o + off + n]:
                out.append("slot %d tag %02x %s %s/%s" % (i, r[o + 0x1f], name,
                           m.ram[o + off:o + off + n].hex(), r[o + off:o + off + n].hex()))
    for k in range(0x80):
        if m.ram[0x76e + 2 * k:0x770 + 2 * k] != r[0x76e + 2 * k:0x770 + 2 * k]:
            out.append("dyn %d %s/%s" % (k, m.ram[0x76e + 2 * k:0x770 + 2 * k].hex(),
                                          r[0x76e + 2 * k:0x770 + 2 * k].hex()))
    return out


def cmd_slots(trace, race, need=300):
    """Level object slots, dynamic terrain codes and the marble run free from the first race
    snapshot: slots/dynamic codes are never copied from the trace again (camera long and
    enemy wall outputs are). While the player is not rolling only the slots run; a
    physics divergence (death, enemy, unported tag) restarts the player from the trace."""
    t = Terrain(race)
    i0 = next(k for k, x in enumerate(trace) if live(x[1]))
    m = Mem(t, trace[i0][1]); m.objects = True
    n = sbad = walls = 0; segs = []; seg = 0; pbad = 0; sync = False; tags = set()
    for (fr, r, _), (fr2, r2, _) in zip(trace[i0:], trace[i0 + 1:]):
        if sync:                                     # player and the rest from the trace
            keep = [(a, b, m.ram[a:b]) for a, b in SLOT_RAM]
            m.ram[:] = r
            for a, b, v in keep: m.ram[a:b] = v
            sync = False
        m.events = []; m.sounds = []
        if any(tb(r, e + 0x18) for e in ENEMIES): m.ram[0x674:0x684] = r[0x674:0x684]
        if live(r):
            tick(m, tb(r2, P + 0xc6), tb(r2, P + 0xc7), camera=tl(r2, 0x40097c) & 0xffffffff)
            err = any(m.sl(P + o) != tl(r2, P + o) for o in (0xc, 0x10, 0x14))
            if err or not live(r2):
                if err and not any(e in DEATH for e in m.events) and live(r2) \
                        and not enemy_near(r, r2):
                    pbad += 1
                    print("tick %d: physics differs %s" % (fr, m.events))
                segs.append(seg); seg = 0; sync = True
            else:
                seg += 1
        else:
            slots_12fd0(m); anim_apply_1344c(m)
            cam = tl(r2, 0x40097c) & 0xffffffff; old = m.rw(0x40097e)
            m.wl(0x40097c, cam)
            if (cam & 0xffff) != old: camera_bands_144e4(m, old, cam & 0xffff)
            sync = True
        n += 1
        walls += any(isinstance(e, tuple) and e[0] == "slot wall" for e in m.events)
        for i in range(SLOT_N):
            if r2[0xa9c + i * SLOT_SIZE + 0x18]: tags.add(r2[0xa9c + i * SLOT_SIZE + 0x1f])
        d = slot_diff(m, r2)
        if d:
            sbad += 1
            if sbad <= 8: print("tick %d: %s" % (fr2, "; ".join(d[:6])))
            for a, b in SLOT_RAM: m.ram[a:b] = r2[a:b]     # resync to keep counting
    segs.append(seg)
    print("slots race %d: %d ticks, slot/dyn state differs in %d, tags seen %s; physics "
          "segments longest %d (%d ticks), slot wall contacts %d, unexplained physics %d"
          % (race, n, sbad, " ".join("%02x" % x for x in sorted(tags)), max(segs), sum(segs),
             walls, pbad))
    return sbad == 0 and pbad == 0 and max(segs) >= need


def cmd_run(trace, race, need=300):
    t = Terrain(race)
    m = None; segs = []; seg = 0; maxerr = 0.0; bad = 0; unp = 0
    stats = {"air": 0, "land": 0, "wall": 0, "side": 0}
    for (fr, r, _), (fr2, r2, _) in zip(trace, trace[1:]):
        if not live(r):
            if m is not None: segs.append(seg); seg = 0; m = None
            continue
        if m is None: m = Mem(t, r)
        oracle(m, r)
        m.events = []; m.sounds = []
        tick(m, tb(r2, P + 0xc6), tb(r2, P + 0xc7))
        err = max(abs(m.sl(P + o) - tl(r2, P + o)) for o in (0xc, 0x10, 0x14)) / 65536
        dead = any(e in DEATH for e in m.events) or not live(r2)
        if dead or err > 0:
            if not dead and not enemy_near(r, r2):
                if unported(m):
                    unp += 1
                else:
                    bad += 1
                    print("diverge at tick %d: %.3f px %s" % (fr, err, m.events))
            segs.append(seg); seg = 0; m = None
            continue
        maxerr = max(maxerr, err); seg += 1
        stats["air"] += m.rb(P + 0x36) == 2
        stats["land"] += any(isinstance(e, tuple) and e[0] == "land" for e in m.events)
        stats["wall"] += 0x42 in m.sounds
        stats["side"] += "side" in m.events
    segs.append(seg)
    segs = [s for s in segs if s]
    print("run race %d: %d ticks exact, segments %s, longest %d, max error %.3f px, %s, "
          "unported slot %d, unexplained %d"
          % (race, sum(segs), segs, max(segs or [0]), maxerr, stats, unp, bad))
    return bad == 0 and max(segs or [0]) >= need


def cmd_start(trace, race):
    """start_259b4 on a cleared RAM == the first race snapshot, over the first segment."""
    t = Terrain(race)
    i = next(k for k, x in enumerate(trace) if live(x[1]))
    m = Mem(t, bytes(0x2000)); start_259b4(m, race); snapshot_start(m)
    ref = Mem(t, trace[i][1])
    n = 0
    for (fr, r, _), (fr2, r2, _) in zip(trace[i:], trace[i + 1:]):
        if not live(r): break
        for mm in (m, ref):
            oracle(mm, r); mm.events = []
            tick(mm, tb(r2, P + 0xc6), tb(r2, P + 0xc7))
        if any(m.sl(P + o) != ref.sl(P + o) for o, _ in FIELDS) or m.rb(P + 0x1a) != ref.rb(P + 0x1a):
            print("start race %d: differs from the snapshot run at tick %d" % (race, n)); return False
        if any(e in DEATH for e in ref.events) or not live(r2) \
                or any(ref.sl(P + o) != tl(r2, P + o) for o in (0xc, 0x10, 0x14)):
            break
        n += 1
    print("start race %d: cell %04x, %d ticks equal to the run from the first snapshot"
          % (race, m.rw(t.desc + 0x14), n))
    return n >= 100


# player bytes the vblank IRQ writes: race timer +0x6a-+0x6f (seconds, tenths, frames),
# trackball deltas and sums +0xc6-+0xc9
DEATH_SKIP = set(range(0x6a, 0x70)) | set(range(0xc6, 0xca))


def death_mem(t, ram):
    m = Mem(t, ram)
    m.deaths = m.objects = m.sprites = True
    return m


def death_inputs(r, r2):
    """Tick inputs of a tick start snapshot r (next snapshot r2): c6, c7, camera long after
    the camera step, 0x400010."""
    return (tb(r, P + 0xc6), tb(r, P + 0xc7), tl(r2, 0x40097c) & 0xffffffff,
            tl(r, 0x400010) & 0xffffffff)


def ref_player_mos(r, v):
    """Player 1 MOs in the MO bank the snapshot shows built (list order from its RAM)."""
    import marble_mo
    words, _ = marble_mo.mo_ram(r, v)
    k = 0
    for _, typ, idx, mos in marble_mo.mo_list_26f3e(marble_mo.Ram(r)):
        if typ == 1 and idx == 0: return words[k:k + len(mos)]
        k += len(mos)
    return []


def death_diff(m, r2, v2):
    """Differences of a death model tick to the next tick start snapshot: player bytes
    (but DEATH_SKIP), RNG word 0x4003a6, slots, dynamic codes, player MOs."""
    a = P - 0x400000
    out = ["+%02x %02x/%02x" % (o, m.ram[a + o], r2[a + o]) for o in range(0xe2)
           if o not in DEATH_SKIP and m.ram[a + o] != r2[a + o]]
    if m.ram[0x3a6:0x3a8] != r2[0x3a6:0x3a8]:
        out.append("rng %s/%s" % (m.ram[0x3a6:0x3a8].hex(), r2[0x3a6:0x3a8].hex()))
    out += slot_diff(m, r2)
    if player_mos(m) != ref_player_mos(r2, v2): out.append("player MOs")
    return out


def cmd_deaths(trace, race, segs):
    """Death model free runs (mame_trace.lua MARBLE_TICK=start), one per segment "FROM:TO"
    (frames, between MARBLE_PLACE teleports; default the whole race): from the first race
    snapshot after FROM, per tick only the inputs (death_inputs) come from the trace. Every
    tick up to TO must equal the next snapshot (death_diff)."""
    ok = True
    rng_in = "rng" in segs
    segs = [x for x in segs if x != "rng"]
    for seg in segs or ["0:%d" % trace[-1][0]]:
        a, b = (int(v) for v in seg.split(":"))
        ok &= death_segment(trace, race, a, b, rng_in)
    return ok


def death_segment(trace, race, start, end, rng_in=False):
    """rng_in: RNG word 0x4003a6 from each snapshot (silly race start: the unported minis
    use the RNG), not compared."""
    t = Terrain(race)
    i0 = next(k for k, x in enumerate(trace) if x[0] > start and live_race(x[1]))
    m = death_mem(t, trace[i0][1])
    states = {}; mos_bad = 0; n = 0
    run = [x for x in trace[i0:] if x[0] <= end]
    for (fr, r, v), (fr2, r2, v2) in zip(run, run[1:]):
        m.events = []; m.sounds = []
        if rng_in: m.ww(0x4003a6, tw(r, 0x4003a6))
        for o in range(0x6a, 0x70): m.wb(P + o, tb(r, P + o))   # race timer: vblank IRQ
        main_tick(m, *death_inputs(r, r2))
        n += 1
        st = m.rb(P + 0x1a); states[st] = states.get(st, 0) + 1
        mos_bad += player_mos(marble_mo_ram(r2)) != ref_player_mos(r2, v2)
        d = [e for e in death_diff(m, r2, v2) if not (rng_in and e.startswith("rng"))]
        if d:
            print("deaths race %d: tick %d (frame %d, state %d) differs: %s %s"
                  % (race, n, fr2, st, "; ".join(d[:8]), m.events))
            return False
    print("deaths race %d: frames %d-%d, %d ticks equal, ticks per state %s; player MO model "
          "vs MO RAM differs in %d" % (race, run[0][0], run[-1][0], n,
                                       dict(sorted(states.items())), mos_bad))
    return mos_bad == 0


def live_race(r):
    """In race play (0x400390 0: not the attract demo, not the race start), player 1 active."""
    return tw(r, 0x400390) == 0 and tb(r, P + 0x18) == 1 and tl(r, P + 0xc) != 0


def marble_mo_ram(r):
    import marble_mo
    return marble_mo.Ram(r)


def cmd_fixture(path):
    ok = True
    for seg in load_fixture(path):
        bad = replay_fixture(seg, fixture_mem(seg, Terrain(seg["race"])))
        print("fixture race %d frame %d: %d ticks, %s" % (seg["race"], seg["frame"],
              len(seg["ticks"]), "exact" if bad is None else "differs at tick %d" % bad))
        ok &= bad is None
    return ok


def cmd_objfixture(path):
    ok = True
    for seg in load_objfixture(path):
        bad = replay_objfixture(seg, fixture_mem(seg, Terrain(seg["race"])))
        print("objects fixture race %d frame %d: %d ticks, %s" % (seg["race"], seg["frame"],
              len(seg["ticks"]), "exact" if bad is None else "differs at tick %d" % bad))
        ok &= bad is None
    return ok


def replay_dfixture(seg, m, rng_in=False, full=None):
    """Run every death fixture tick on m (rng_in: tube fixture, RNG word set before each
    tick; full (default rng_in): all enemies run); index of the first tick that differs, or
    None."""
    if rng_in if full is None else full:
        import marble_enemy
        marble_enemy.setup(m)
    else:
        m.deaths = m.objects = m.sprites = True
    for k, tick in enumerate(seg["ticks"]):
        c6, c7, cam, frames = tick[:4]
        if rng_in: m.ww(0x4003a6, tick[4])
        main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
        if death_state(m) != tick[-1]: return k
    return None


def cmd_dfixture(path, rng_in=False, full=None, arr=None):
    ok = True
    for seg in load_dfixture(path, rng_in, arr):
        bad = replay_dfixture(seg, fixture_mem(seg, Terrain(seg["race"])), rng_in, full)
        print("deaths fixture race %d frame %d: %d ticks, %s" % (seg["race"], seg["frame"],
              len(seg["ticks"]), "exact" if bad is None else "differs at tick %d" % bad))
        ok &= bad is None
    return ok


def main(argv):
    if len(argv) == 3 and argv[1] == "dfixture":
        return 0 if cmd_dfixture(argv[2]) else 1
    if len(argv) == 3 and argv[1] == "tfixture":
        return 0 if cmd_dfixture(argv[2], True) else 1
    if len(argv) == 3 and argv[1] == "afixture":
        return 0 if cmd_dfixture(argv[2], False, True, "marble_aerials_segments") else 1
    if len(argv) == 3 and argv[1] == "tbfixture":
        return 0 if cmd_dfixture(argv[2], False, True, "marble_bonuses_segments") else 1
    if len(argv) == 3 and argv[1] == "fixture":
        return 0 if cmd_fixture(argv[2]) else 1
    if len(argv) == 3 and argv[1] == "objfixture":
        return 0 if cmd_objfixture(argv[2]) else 1
    if len(argv) < 4 or argv[1] not in ("terrain", "ticks", "run", "start", "occl", "slots",
                                        "deaths"):
        print(__doc__); return 2
    trace = load_trace(argv[2]); race = int(argv[3])
    if argv[1] == "terrain": ok = cmd_terrain(trace, race)
    elif argv[1] == "deaths": ok = cmd_deaths(trace, race, argv[4:])
    elif argv[1] == "occl": ok = cmd_occl(trace, race)
    elif argv[1] == "slots": ok = cmd_slots(trace, race, int(argv[4]) if len(argv) > 4 else 300)
    elif argv[1] == "ticks": ok = cmd_ticks(trace, race)
    elif argv[1] == "start": ok = cmd_start(trace, race)
    else: ok = cmd_run(trace, race, int(argv[4]) if len(argv) > 4 else 300)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
