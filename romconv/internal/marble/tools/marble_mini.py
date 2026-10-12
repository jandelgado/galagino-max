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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import marble_sim as ms
from marble_sim import s8, s16, s32, rom_l, rom_w, PROG

MINIS = tuple(0x401890 + 0x28 * k for k in range(9))   # = table 0x1f096
MINI_SIZE = 0x28
SILLY = 4
STEELIE, SLINKY, ACID = 7, 8, 9          # +0x25 = draw type; palette sets 5, 6, 4
Z0 = 0x3f6e0000
FLOOR_Z = 0x3f6e                         # player z word that switches minis to kind 1
# 0x1953e (disassembly): walk anim by type and the v word signs: y > 0, y < 0, x > 0, x < 0,
# standing
WALK = {SLINKY: (0x219b2, 0x21a32, 0x2197a, 0x219ea, 0x2194e),
        ACID: (0x21e3e, 0x21eb6, 0x21d4e, 0x21dc6, 0x21f06)}
WALK_STEELIE = 0x21f72
COLLECTED = {STEELIE: 0x21f8a, SLINKY: 0x21a62}                  # 0x194ba kind 2, else ...
COLLECTED_OTHER = 0x21efe
HIT_ANIM = {ACID: 0x21efe, STEELIE: 0x21f7a}                    # 0x1924e
DX, DY = 0x244b6, 0x244d6                # 0x19976: step words per direction (<< 8)
GRID = 0x24496                           # 0x19460: solid cell bitmap (16 rows from 0x59/0x5a)
STUCK = 0x10
MINI_BAND = (0x1d, 0x38)                 # 0x144e4 mode 4: spawn entering, clear leaving


def band_144e4(m, a, b):
    """0x144e4 mode 4 mini part (before the bird flag): entering 0x1d-0x38 spawns all nine
    (0x18ffa), leaving clears them (0x190ee)."""
    def inside(v): return MINI_BAND[0] <= s8(v) <= MINI_BAND[1]
    if not inside(a) and inside(b): spawn_18ffa(m)
    if inside(a) and not inside(b): clear_190ee(m)


def coords_199d6(m, a2):
    """Screen position +0x20 (x word, y word) as slot objects (0x400690/2 written)."""
    x, y = m.rw(a2 + 0xc), m.rw(a2 + 0x10)
    m.ww(0x400690, x); m.ww(0x400692, y)
    sx = (y - x + 0x88) & 0xffff
    sy = (m.rw(0x40097e) + m.rw(a2 + 0x14) + 0x54 - ((s16(x) + s16(y)) >> 1)) & 0xffff
    m.wl(a2 + 0x20, sx << 16 | sy)


def blocked_1937c(m, a2):
    """1: another live mini (not collected) within 12 px on both axes, or a solid cell of
    0x24496 (outside its 16 x 16 window counts as solid); 0: free."""
    x, y = m.sw(a2 + 0xc), m.sw(a2 + 0x10)
    for o in MINIS:
        if o == a2 or not m.rb(o + 0x18) or m.rb(o + 0x1a) == 2: continue
        if abs(m.sw(o + 0xc) - x) < 0xc and abs(m.sw(o + 0x10) - y) < 0xc: return 1
    cx = ((x >> 3) - 0x59) & 0xff; cy = ((y >> 3) - 0x5a) & 0xff
    if not 0 <= s8(cx) <= 0xf or not 0 <= s8(cy) <= 0xf: return 1
    return 1 if s16(rom_w(GRID + 2 * s8(cy))) & (1 << (cx & 0x3f)) else 0


def step_19976(m, a2):
    """One step in direction +0x26 (table words << 8); v = the step (steelie: / 4)."""
    d = s8(m.rb(a2 + 0x26))
    dx = s16(rom_w(DX + 2 * d)) << 8; dy = s16(rom_w(DY + 2 * d)) << 8
    m.wl(a2 + 0xc, m.rl(a2 + 0xc) + dx); m.wl(a2 + 0x10, m.rl(a2 + 0x10) + dy)
    if m.rb(a2 + 0x25) == STEELIE: dx >>= 2; dy >>= 2
    m.wl(a2, dx & 0xffffffff); m.wl(a2 + 4, dy & 0xffffffff)


def turn(m, a2, iters, avoid_back):
    """0x19692 (iters 12) / 0x198bc (iters 9, avoid_back): a step in direction +0x26 sets v;
    blocked, the direction turns by 1 (steelie) or 4 (every fourth try) until a free one;
    none: +0x26 = 0x10, v = 0. The position is restored in every case."""
    if m.rb(a2 + 0x26) == STUCK: return
    x0, y0 = m.rl(a2 + 0xc), m.rl(a2 + 0x10)
    fine = m.rb(a2 + 0x25) == STEELIE
    step = 1 if fine else 4
    def restore(): m.wl(a2 + 0xc, x0); m.wl(a2 + 0x10, y0)
    step_19976(m, a2)
    if not blocked_1937c(m, a2): restore(); return
    d4 = m.rb(a2 + 0x26)
    if avoid_back: m.wb(a2 + 0x26, d4 - 4)
    for d2 in range(iters):
        if not (fine or d2 & 3 == 0): continue
        restore()
        m.wb(a2 + 0x26, (m.rb(a2 + 0x26) + step) & 0xf)
        if avoid_back and m.rb(a2 + 0x26) == d4: continue
        step_19976(m, a2)
        if not blocked_1937c(m, a2): restore(); return
    m.wb(a2 + 0x26, STUCK); m.wl(a2 + 4, 0); m.wl(a2, 0)
    restore()


def wander_1960e(m, a2):
    """Kind 0: steelie jitters its direction by -2..2, the others pick a new one (axis by v,
    side by rng(2); acid sometimes stops: 1 in 4); then 0x19692."""
    if m.rb(a2 + 0x25) == STEELIE:
        m.wb(a2 + 0x26, (m.rb(a2 + 0x26) + ms.rng_13a98(m, 5) - 2) & 0xf)
    else:
        r = ms.rng_13a98(m, 2)
        m.wb(a2 + 0x26, ((r << 3) + (4 if m.rl(a2) else 0)) & 0xff)
        if ms.rng_13a98(m, 4) == 0 and m.rb(a2 + 0x25) == ACID:
            m.wb(a2 + 0x26, STUCK); m.wl(a2 + 4, 0); m.wl(a2, 0)
    turn(m, a2, 12, False)


def chase_1973c(m, a2):
    """Kind 1 (disassembly 0x1973c): the nearest player (octant distance, below 0x140, else
    player 1); steelie: direction away from it (16ths by the axis ratio and quadrant), the
    others: one axis towards it; then 0x198bc."""
    best, p = 0x140, ms.P
    for k in range(m.rw(0x400396)):
        a = ms.P + 0xe2 * k
        if not m.rb(a + 0x18): continue
        ax = (abs(s16(m.rw(a2 + 0xc) - m.rw(a + 0xc))) << 4) & 0xffff
        ay = (abs(s16(m.rw(a2 + 0x10) - m.rw(a + 0x10))) << 4) & 0xffff
        d = ((ay >> 3) * 3 + ax if ax >= ay else (ax >> 3) * 3 + ay) & 0xffff
        if d < best: best, p = d, a
    dx = s16(m.rw(a2 + 0xc) - m.rw(p + 0xc)); dy = s16(m.rw(a2 + 0x10) - m.rw(p + 0x10))
    if m.rb(a2 + 0x25) == STEELIE:
        ax, ay = (abs(dx) & 0xffff) >> 4, (abs(dy) & 0xffff) >> 4
        d = 0 if ay == 0 else (1 if ax > ay else (3 if ay > ax else 2))
        if dx > 0 and dy < 0: d += 12
        elif dx < 0 and dy < 0: d += 8
        elif dx < 0 and dy > 0: d += 4
    else:
        dx >>= 4; dy >>= 4
        d = 4 if dy > 0 else (12 if dy < 0 else (0 if dx > 0 else 8))
    m.wb(a2 + 0x26, d)
    turn(m, a2, 9, True)


def walk_1953e(m, a2):
    """Walk anim of a2's type: steelie one list, slinky/acid by v (y sign first, then x)."""
    t = m.rb(a2 + 0x25)
    if t == STEELIE: m.wl(a2 + 0x1c, WALK_STEELIE); return
    if t not in WALK: return
    vx, vy = m.sw(a2), m.sw(a2 + 4)
    k = 0 if vy > 0 else (1 if vy < 0 else (2 if vx > 0 else (3 if vx < 0 else 4)))
    m.wl(a2 + 0x1c, WALK[t][k])


def dispatch_194ba(m, a2):
    """By kind: 0 wander, 1 chase (then the type's walk anim 0x1953e), 2 the collected anim."""
    k = s8(m.rb(a2 + 0x1a)); t = m.rb(a2 + 0x25)
    if k in (0, 1):
        (wander_1960e if k == 0 else chase_1973c)(m, a2)
        walk_1953e(m, a2)
    elif k == 2:
        m.wl(a2 + 0x1c, COLLECTED.get(t, COLLECTED_OTHER))


def spawn_18ffa(m):
    """All nine at random free cells (rng(0x20) x/y per try) of the mini field, 3 of each
    type, z 0x3f6e, kind 0, then 0x194ba, 0x199d6, draw list insert; not while any is live."""
    if any(m.rb(a + 0x18) == 1 for a in MINIS): return
    for i, a in enumerate(MINIS):
        m.wb(a + 0x25, STEELIE if i < 3 else (SLINKY if i < 6 else ACID))
        m.wb(a + 0x18, 1); m.wb(a + 0x1a, 0)
        while True:
            m.wl(a + 0xc, (((ms.rng_13a98(m, 0x20) << 2) + 0x2ca) << 16) & 0xffffffff)
            m.wl(a + 0x10, (((ms.rng_13a98(m, 0x20) << 2) + 0x2d2) << 16) & 0xffffffff)
            if not blocked_1937c(m, a): break
        m.wl(a + 0x14, Z0); m.wl(a + 8, 0); m.wl(a + 4, 0); m.wl(a, 0); m.wb(a + 0x1b, 0)
        dispatch_194ba(m, a)
        coords_199d6(m, a)
        if m.drawlist: m.drawlist.insert(m, m.rb(a + 0x25), m.rb(a + 0x19))
    m.events.append(("mini spawn",))


def clear_190ee(m):
    """All nine out (+0x18 = 0, draw list removal by type +0x25)."""
    for a in MINIS:
        m.wb(a + 0x18, 0)
        if m.drawlist: m.drawlist.remove(m, m.rb(a + 0x25), m.rb(a + 0x19))
    m.events.append(("mini clear",))


def update_1912c(m):
    """Silly race: kind 1 for all (not collected) while a player stands on the 0x3f6e floor in
    zone 1; per live mini the ticker (kind 1: every tick, else every 3rd): steelie steps by v
    (every 4th step a new direction; collected: anim), the others at each anim list end;
    always 0x199d6."""
    if m.rw(0x400394) != SILLY: return
    near = 0
    for k in range(m.rw(0x400396)):
        a = ms.P + 0xe2 * k
        if m.rb(a + 0x18) == 1 and m.rw(a + 0x14) == FLOOR_Z and m.rb(a + 0x1b) == 1: near = 1
    for a2 in MINIS:
        if not m.rb(a2 + 0x18): continue
        m.wb(a2 + 0x24, m.rb(a2 + 0x24) + 1)
        if m.rb(a2 + 0x1a) != 2: m.wb(a2 + 0x1a, near)
        if (1 if m.rb(a2 + 0x1a) == 1 else 3) > s8(m.rb(a2 + 0x24)):
            coords_199d6(m, a2); continue
        m.wb(a2 + 0x24, 0)
        if m.rb(a2 + 0x25) == STEELIE and m.rb(a2 + 0x1a) == 2:
            m.wl(a2 + 0x1c, m.rl(a2 + 0x1c) + 4)
            if rom_l(m.rl(a2 + 0x1c)) == 0xffffffff: dispatch_194ba(m, a2)
        elif m.rb(a2 + 0x25) == STEELIE:
            m.wl(a2 + 0xc, m.rl(a2 + 0xc) + m.rl(a2)); m.wl(a2 + 0x10, m.rl(a2 + 0x10) + m.rl(a2 + 4))
            m.wb(a2 + 0x1b, m.rb(a2 + 0x1b) + 1)
            if s8(m.rb(a2 + 0x1b)) >= 4:
                dispatch_194ba(m, a2); m.wb(a2 + 0x1b, 0)
        else:
            m.wl(a2 + 0x1c, m.rl(a2 + 0x1c) + 4)
            if rom_l(m.rl(a2 + 0x1c)) == 0xffffffff:
                m.wl(a2 + 0xc, m.rl(a2 + 0xc) + m.rl(a2)); m.wl(a2 + 0x10, m.rl(a2 + 0x10) + m.rl(a2 + 4))
                dispatch_194ba(m, a2)
        coords_199d6(m, a2)


def contact_1924e(m, a2):
    """Player a2 in zone 1 (silly race) against every live, not collected mini: |d| below 4
    (steelie 5) px on both axes: collected (kind 2, v 0, anim), +0x6a += 3 (race seconds),
    points 0x285b0(14) and 500 (0x28608)."""
    if m.rw(0x400394) != SILLY or m.rb(a2 + 0x1b) != 1: return
    for a3 in MINIS:
        if not m.rb(a3 + 0x18) or m.rb(a3 + 0x1a) == 2: continue
        r = 5 if m.rb(a3 + 0x25) == STEELIE else 4
        if r <= abs(m.sw(0x400690) - m.sw(a3 + 0xc)) or r <= abs(m.sw(0x400692) - m.sw(a3 + 0x10)):
            continue
        m.wb(a3 + 0x1a, 2); m.wl(a3 + 4, 0); m.wl(a3, 0)
        dispatch_194ba(m, a3)
        t = m.rb(a3 + 0x25)
        if t in HIT_ANIM: m.wl(a3 + 0x1c, HIT_ANIM[t])
        m.ww(a2 + 0x6a, m.rw(a2 + 0x6a) + 3)
        ms.score_285b0(m, a2, 0xe)
        m.wl(a2 + 0xbc, (m.rl(a2 + 0xbc) + 500) & 0xffffffff)
        m.wb(0x40039c, m.rb(0x40039c) | ((1 << m.rb(a2 + 0x19)) & 0xff))
        m.events.append(("mini collected", a3))


def enable(m):
    """Minis on: band spawn/clear, 0x1912c in main_tick, 0x1924e."""
    m.minis = True


# ---------------------------------------------------------------- trace checks

def mini_mos(m, i):
    import marble_enemy, marble_mo
    t = m.rb(MINIS[i] + 0x25)
    return marble_mo.other_mos(m, t, i) if (t, i) in marble_enemy.drawn(m) else []


def mini_diff(m, r2, v2):
    """bird_diff (player, enemies, birds, draw list, RNG via the player diff), the nine mini
    records, their MOs."""
    import marble_bird, marble_slinky
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


# ---------------------------------------------------------------- fixture

def mini_obj(m, i):
    """Mini fixture check values: +0x18, +0x1a kind, +0x25 type, x, y, z, sx, sy (+0x20/+0x22),
    ticker +0x24, steps +0x1b, anim cursor +0x1c, direction +0x26, v x/y, the drawn frame
    header (rl(+0x1c), 0 if not in the draw list), its MOs."""
    import marble_enemy
    a = MINIS[i]
    t = m.rb(a + 0x25)
    shown = m.rb(a + 0x18) and (t, i) in marble_enemy.drawn(m)
    return (m.rb(a + 0x18), m.rb(a + 0x1a), t, m.sl(a + 0xc), m.sl(a + 0x10), m.sl(a + 0x14),
            m.sw(a + 0x20), m.sw(a + 0x22), m.rb(a + 0x24), m.rb(a + 0x1b), m.rl(a + 0x1c),
            m.rb(a + 0x26), m.sl(a), m.sl(a + 4), rom_l(m.rl(a + 0x1c)) if shown else 0,
            tuple(mini_mos(m, i)))


def tick_state(m):
    """Fixture values of one tick: player, the nine minis, the draw list."""
    import marble_enemy
    return (ms.death_state(m),) + tuple(mini_obj(m, i) for i in range(9)) + \
        (tuple(marble_enemy.drawn(m)),)


PW, NW = 43, 17                        # ints per player, per mini in a fixture tick


def load_mnfixture(path):
    """Segments of source/test/test_marble_minis/marble_minis_trace.h (make_fixture.py
    mnwrite): [{"race", "frame", "ram", "ticks": [(c6, c7, camera, frames, tick_state)]}]."""
    import re
    text = open(path).read()
    num = lambda s: [int(v, 0) for v in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", s)]
    arrays = {name: body for name, body in
              re.findall(r"static const \w+ (\w+)\[\] = \{(.*?)\n?\};", text, re.S)}
    segs = []
    for race, frame, nram, ramname, nticks, tickname, nmos, moname, nlist, listname in re.findall(
            r"\{ *(\d+), *(\d+), *(\d+), *(\w+), *(\d+), *(\w+), *(\d+), *(\w+), *(\d+), *(\w+) *\}",
            arrays["marble_mini_segments"]):
        ram = [(int(addr, 0), bytes(num(arrays[data]))) for addr, _, data in
               re.findall(r"\{ *(0x[0-9a-f]+), *(\d+), *(\w+) *\}", arrays[ramname])]
        mw = num(arrays[moname]); lw = num(arrays[listname])
        mos = [tuple(mw[k:k + 3]) for k in range(0, len(mw), 3)]
        pairs = [tuple(lw[k:k + 2]) for k in range(0, len(lw), 2)]
        v = num(arrays[tickname]); w = 4 + PW + 9 * NW + 2
        assert len(v) == w * int(nticks)
        ticks = []
        for k in range(0, len(v), w):
            t = v[k:k + w]
            c6, c7, cam, frames = t[:4]
            p = t[4:4 + PW]
            player = tuple(p[:10]) + (bytes(p[10:40]), p[40], tuple(mos[p[42]:p[42] + p[41]]))
            objs = []
            for j in range(9):
                q = t[4 + PW + NW * j:4 + PW + NW * (j + 1)]
                objs.append(tuple(q[:15]) + (tuple(mos[q[16]:q[16] + q[15]]),))
            n, first = t[-2:]
            ticks.append((c6, c7, cam, frames, (player,) + tuple(objs) +
                          (tuple(pairs[first:first + n]),)))
        segs.append({"race": int(race), "frame": int(frame), "ram": ram, "ticks": ticks})
    return segs


def replay_mnfixture(seg, m):
    """Run every mini fixture tick on m (main_tick + 0x26f3e sort); index of the first tick
    that differs, or None."""
    import marble_enemy, marble_mo
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


def sweep(m, race):
    """Race sweep with all modelled enemies (marble_enemy.setup(m) done) on a cleared RAM: the
    remake race start, then main_tick + 0x26f3e sort per camera step of marble_mo.race_sweep,
    player idle. All nine live exactly while the band is in 0x1d-0x38 (silly race), the draw
    list. Returns (per tick (live minis, draw list), spawns, clears)."""
    import marble_enemy, marble_mo
    ms.begin_race(m, race)
    top, rng = ms.rom_w(m.t.desc + 0x10), ms.rom_w(m.t.desc + 0x12)
    cams = list(range(top, top + rng + 1, marble_mo.SWEEP_STEP))
    if race == SILLY: cams.reverse()
    cams = cams + cams[::-1] + cams
    out, spawns, clears = [], 0, 0
    live0 = any(m.rb(a + 0x18) for a in MINIS)
    for k, cam in enumerate(cams):
        before = any(m.rb(a + 0x18) for a in MINIS)
        ms.main_tick(m, 0, 0, cam & 0xffffffff, k + 1)
        marble_mo.boxes_and_sort(m)
        lst = marble_enemy.drawn(m)
        live = [m.rb(a + 0x18) for a in MINIS]
        band = s16(cam - top) >> 4
        want = race == SILLY and MINI_BAND[0] <= band <= MINI_BAND[1]
        if any(live) != want or (any(live) and not all(live)):
            raise SystemExit("race %d cam %d band %d: minis live %s" % (race, cam, band, live))
        spawns += not before and all(live); clears += before and not any(live)
        for i, a in enumerate(MINIS):
            if bool(live[i]) != ((m.rb(a + 0x25), i) in lst):
                raise SystemExit("race %d cam %d: mini %d live %d, draw list %s" % (race, cam, i, live[i], lst))
        out.append((tuple(live), tuple(lst)))
    return out, spawns + live0, clears


def cmd_sweep():
    import marble_enemy
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
