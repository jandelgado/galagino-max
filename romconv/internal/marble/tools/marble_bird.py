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
import marble_sim as ms
from marble_sim import s8, s16, s32, rom_l, PROG

BIRDS = tuple(0x4019f8 + 0x38 * k for k in range(10))   # = table 0x1f0ba
BIRD_SIZE = 0x38
MINIS = tuple(0x401890 + 0x28 * k for k in range(9))    # 4b-4b; bird traces need them empty
SPAWN_ON = 0x400762                    # set while the camera band is in 3-0x1b (0x144e4)
POINTS = 0x244f6                       # 5 spawn cells (x, y bytes)
Z0 = 0x3fd80000
ANIM_SPAWN, ANIM_WALK, ANIM_LAND, ANIM_CARRY = 0x224ca, 0x224d6, 0x224e2, 0x22546
V_FAST, V_SLOW = 0xfffc0000, 0xfffe0000
TYPE = 0xf
SILLY = 4


def band_flag_144e4(m, a, b):
    """0x144e4 mode 4, after the slot spawner: bird spawning on while band in 3-0x1b. The mini
    spawner/clear 0x18ffa/0x190ee (band 0x1d-0x38) come with 4b-4b."""
    def inside(v): return 3 <= s8(v) <= 0x1b
    if not inside(a) and inside(b): m.wb(SPAWN_ON, 1)
    elif inside(a) and not inside(b): m.wb(SPAWN_ON, 0)


def screen_19e42(m, a2):
    """Screen position +0x20 (x word, y word) as slot objects; masks 0x264aa mode 3 at cells x
    0x29/0x31/0x39, y >= 0x34, else the 3 entries at +0x26 cleared."""
    x, y = m.rw(a2 + 0xc), m.rw(a2 + 0x10)
    m.ww(0x400690, x); m.ww(0x400692, y)
    sx = (y - x + 0x88) & 0xffff
    sy = (m.rw(0x40097e) + m.rw(a2 + 0x14) + 0x54 - ((s16(x) + s16(y)) >> 1)) & 0xffff
    m.wl(a2 + 0x20, sx << 16 | sy)
    cx, cy = (s16(x) >> 3) & 0xff, s8(s16(y) >> 3)
    if cx in (0x39, 0x31, 0x29) and cy >= 0x34:
        ms.occlusion_264aa(m, a2, 3)
    else:
        for i in range(3): m.ww(a2 + 0x26 + 6 * i, 0)


def spawn_19a40(m):
    """Two passes over the 5 spawn cells, at most one spawn per pass: a cell spawns when at most
    pass-many live birds stand in its x column (one: only if that bird is 4+ cells below the
    spawn row); the first free slot (lowest index) takes it; none free ends the scan."""
    for d4 in range(2):
        for d5 in range(5):
            cx, cy = s8(PROG[POINTS + 2 * d5]), s8(PROG[POINTS + 2 * d5 + 1])
            d3, first = 0, 0
            for k, a in enumerate(BIRDS):
                if m.rb(a + 0x18) == 1 and (m.sw(a + 0xc) >> 3) & 0xffff == cx & 0xffff:
                    d3 += 1
                    if d3 < 2: first = k
            if d3 > d4: continue
            if d3 == 1 and 4 > cy - (m.sw(BIRDS[first] + 0x10) >> 3): continue
            free = next((a for a in BIRDS if m.rb(a + 0x18) != 1), None)
            if free is None: return
            a = free
            m.wb(a + 0x18, 1); m.wb(a + 0x1a, 1)
            m.wl(a + 0xc, ((cx << 19) + 0x40000) & 0xffffffff)
            m.wl(a + 0x10, ((cy << 19) + 0x40000) & 0xffffffff)
            m.wl(a + 0x14, Z0); m.wb(a + 0x24, 0); m.wb(a + 0x1b, 0); m.wb(a + 0x25, 8)
            m.wl(a + 0x1c, ANIM_SPAWN); m.wl(a + 4, V_FAST)
            screen_19e42(m, a)
            if m.drawlist: m.drawlist.insert(m, TYPE, m.rb(a + 0x19))
            m.events.append(("bird spawn", a))
            break                                    # one spawn per pass


def update_19baa(m):
    """Silly race only: spawn every 8 frames while 0x400762, then per live bird the anim
    ticker (list end: walk again with a new speed and loop count, or removal after landing /
    carrying), the walk (y += v; terrain above z: back to the 4 px grid, landing anim), the
    screen and masks."""
    if m.rw(0x400394) != SILLY: return
    if m.rb(SPAWN_ON) and m.rl(0x400010) & 7 == 0: spawn_19a40(m)
    for a in BIRDS:
        if not m.rb(a + 0x18): continue
        m.wb(a + 0x24, m.rb(a + 0x24) + 1)
        if s8(m.rb(a + 0x25)) <= s8(m.rb(a + 0x24)):
            m.wb(a + 0x24, 0); m.wl(a + 0x1c, m.rl(a + 0x1c) + 4)
            if rom_l(m.rl(a + 0x1c)) == 0xffffffff and m.rb(a + 0x1a) != 2:
                ahead = any(o != a and m.rb(o + 0x18) == 1 and m.rw(o + 0xc) == m.rw(a + 0xc)
                            and m.sw(a + 0x10) > m.sw(o + 0x10) for o in BIRDS)
                m.wl(a + 0x1c, ANIM_WALK); m.wb(a + 0x1a, 0)
                if m.rb(a + 0x1b): m.wb(a + 0x1b, m.rb(a + 0x1b) - 1)
                if not m.rb(a + 0x1b):
                    if m.rl(a + 4) == V_SLOW and not ahead:
                        m.wb(a + 0x25, 1); m.wl(a + 4, V_FAST)
                        m.wb(a + 0x1b, ms.rng_13a98(m, 2) + 4)
                    else:
                        m.wb(a + 0x25, 4); m.wl(a + 4, V_SLOW)
                        m.wb(a + 0x1b, ms.rng_13a98(m, 2) + 1)
            elif rom_l(m.rl(a + 0x1c)) == 0xffffffff and m.rb(a + 0x1a) == 2:
                m.wb(a + 0x18, 0)
                if m.drawlist: m.drawlist.remove(m, TYPE, m.rb(a + 0x19))
                m.events.append(("bird gone", a))
        if m.rb(a + 0x1a) == 0:
            y0 = m.rl(a + 0x10)
            m.wl(a + 0x10, y0 + m.rl(a + 4))
            import marble_slinky
            if s32(marble_slinky.height(m, a)) > m.sl(a + 0x14):
                m.wl(a + 0x10, y0 & 0xfffc0000); m.wl(a + 0x1c, ANIM_LAND)
                m.wb(a + 0x24, 0); m.wb(a + 0x25, 1); m.wb(a + 0x1a, 2)
                m.events.append(("bird lands", a))
        screen_19e42(m, a)


def contact_19d94(m, a2):
    """Player a2 vs every walking bird (silly race): marble point (0x400690/0x400692) in the
    12 x 12 box x - 6, y - 4: bird carries it (state 2, anim 0x22546), player state 0xb,
    +0x57 = 0x66 (orbit length)."""
    if m.rw(0x400394) != SILLY: return
    mx, my = m.sw(0x400690), m.sw(0x400692)
    for a in BIRDS:
        if not m.rb(a + 0x18) or m.rb(a + 0x1a): continue
        left = s16(m.rw(a + 0xc) - 6); top = s16(m.rw(a + 0x10) - 4)
        if not (left <= mx < s16(left + 0xc) and top <= my < s16(top + 0xc)): continue
        m.wb(a + 0x1a, 2); m.wl(a + 0x1c, ANIM_CARRY); m.wb(a + 0x25, 4); m.wb(a + 0x24, 0)
        m.wb(a2 + 0x1a, 0xb); m.wb(a2 + 0x57, 0x66)
        m.events.append(("bird carries", a))


def carried_25876(m, a2):
    """Player state 0xb: screen, orbit stars 0x13ade; at its end lost (0x25bae(4))."""
    ms.proj_1b9cc(m, a2, True)
    if ms.orbit_13ade(m, a2):
        m.wb(a2 + 0x57, 0x65)
        ms.enter_25bae(m, a2, 4)


def enable(m):
    """Birds on: band flag, 0x19baa in main_tick, 0x19d94, player state 0xb."""
    m.birds = True


# ---------------------------------------------------------------- trace checks

def bird_mos(m, i):
    import marble_enemy, marble_mo
    return marble_mo.other_mos(m, TYPE, i) if (TYPE, i) in marble_enemy.drawn(m) else []


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


# ---------------------------------------------------------------- fixture

def bird_obj(m, i):
    """Bird fixture check values: +0x18, +0x1a, x, y, z, sx, sy (+0x20/+0x22), ticker
    +0x24/+0x25, loops +0x1b, anim cursor +0x1c, the drawn frame header (rl(+0x1c), 0 if not
    in the draw list), masks +0x26 (18 bytes), its MOs."""
    import marble_enemy
    a = BIRDS[i]
    shown = m.rb(a + 0x18) and (TYPE, i) in marble_enemy.drawn(m)
    return (m.rb(a + 0x18), m.rb(a + 0x1a), m.sl(a + 0xc), m.sl(a + 0x10), m.sl(a + 0x14),
            m.sw(a + 0x20), m.sw(a + 0x22), m.rw(a + 0x24), m.rb(a + 0x1b), m.rl(a + 0x1c),
            rom_l(m.rl(a + 0x1c)) if shown else 0,
            bytes(m.ram[a + 0x26 - 0x400000:a + 0x38 - 0x400000]), tuple(bird_mos(m, i)))


def tick_state(m):
    """Fixture values of one tick: player, the ten birds, 0x400762, the draw list."""
    import marble_enemy
    return (ms.death_state(m),) + tuple(bird_obj(m, i) for i in range(10)) + \
        (m.rb(SPAWN_ON), tuple(marble_enemy.drawn(m)))


PW, BW = 43, 31                        # ints per player, per bird in a fixture tick


def load_bdfixture(path):
    """Segments of source/test/test_marble_birds/marble_birds_trace.h (make_fixture.py bdwrite):
    [{"race", "frame", "ram", "ticks": [(c6, c7, camera, frames, tick_state)]}]."""
    import re
    text = open(path).read()
    num = lambda s: [int(v, 0) for v in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", s)]
    arrays = {name: body for name, body in
              re.findall(r"static const \w+ (\w+)\[\] = \{(.*?)\n?\};", text, re.S)}
    segs = []
    for race, frame, nram, ramname, nticks, tickname, nmos, moname, nlist, listname in re.findall(
            r"\{ *(\d+), *(\d+), *(\d+), *(\w+), *(\d+), *(\w+), *(\d+), *(\w+), *(\d+), *(\w+) *\}",
            arrays["marble_bird_segments"]):
        ram = [(int(addr, 0), bytes(num(arrays[data]))) for addr, _, data in
               re.findall(r"\{ *(0x[0-9a-f]+), *(\d+), *(\w+) *\}", arrays[ramname])]
        mw = num(arrays[moname]); lw = num(arrays[listname])
        mos = [tuple(mw[k:k + 3]) for k in range(0, len(mw), 3)]
        pairs = [tuple(lw[k:k + 2]) for k in range(0, len(lw), 2)]
        v = num(arrays[tickname]); w = 4 + PW + 10 * BW + 3
        assert len(v) == w * int(nticks)
        ticks = []
        for k in range(0, len(v), w):
            t = v[k:k + w]
            c6, c7, cam, frames = t[:4]
            p = t[4:4 + PW]
            player = tuple(p[:10]) + (bytes(p[10:40]), p[40], tuple(mos[p[42]:p[42] + p[41]]))
            objs = []
            for j in range(10):
                q = t[4 + PW + BW * j:4 + PW + BW * (j + 1)]
                objs.append(tuple(q[:11]) + (bytes(q[11:29]), tuple(mos[q[30]:q[30] + q[29]])))
            flag, n, first = t[-3:]
            ticks.append((c6, c7, cam, frames, (player,) + tuple(objs) +
                          (flag, tuple(pairs[first:first + n]))))
        segs.append({"race": int(race), "frame": int(frame), "ram": ram, "ticks": ticks})
    return segs


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


def sweep(m, race):
    """Race sweep with all modelled enemies (marble_enemy.setup(m) done) on a cleared RAM: the
    remake race start, then main_tick + 0x26f3e sort per camera step of marble_mo.race_sweep,
    player idle. Checks 0x400762 against the camera band, birds only in the silly race, the
    draw list. Returns (per tick (live birds, draw list), spawns, removals)."""
    import marble_enemy, marble_mo
    ms.begin_race(m, race)
    top, rng = ms.rom_w(m.t.desc + 0x10), ms.rom_w(m.t.desc + 0x12)
    cams = list(range(top, top + rng + 1, marble_mo.SWEEP_STEP))
    if race == SILLY: cams.reverse()
    cams = cams + cams[::-1] + cams
    out, spawns, removals = [], 0, 0
    for k, cam in enumerate(cams):
        before = [m.rb(a + 0x18) for a in BIRDS]
        ms.main_tick(m, 0, 0, cam & 0xffffffff, k + 1)
        marble_mo.boxes_and_sort(m)
        lst = marble_enemy.drawn(m)
        band = s16(cam - top) >> 4
        if m.rb(SPAWN_ON) != (race == SILLY and 3 <= band <= 0x1b):
            raise SystemExit("race %d cam %d band %d: 0x400762 %d" % (race, cam, band, m.rb(SPAWN_ON)))
        for i, a in enumerate(BIRDS):
            live = m.rb(a + 0x18)
            if live and race != SILLY: raise SystemExit("race %d: bird %d live" % (race, i))
            spawns += not before[i] and live
            removals += before[i] and not live
            if bool(live) != ((TYPE, i) in lst):
                raise SystemExit("race %d cam %d: bird %d live %d, draw list %s" % (race, cam, i, live, lst))
        out.append((tuple(m.rb(a + 0x18) for a in BIRDS), tuple(lst)))
    return out, spawns, removals


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
