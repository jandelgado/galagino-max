"""Marble Madness aerial race pistons (Phase 4b-5): table 0x401650 (36 x 0x10, draw type
0x29). 1:1 ports named by ROM address from Marble Love (state-sub-186ac.ts,
read-abs-long-1844a.ts, state-sub-1881c.ts, sprite-coords.ts computeSpriteCoords_v4 0x18972,
sprite-derive.ts 0x1bb28) on the work RAM image of marble_sim.Mem; the 0x1881c result (cell
refresh 0x1bab2 in 0x121b8, the TS drops it) from the disassembly 0x123ec-0x12400. Facts:
MARBLE.md "Enemies".

Entry fields: +0 index, +2 timer word (counts down; 0xffff = up, anim running), +4/+5 cell x/y
(bytes), +6 top z word, +8 anim cursor (from 0x21342), +0xc screen (x word, y word).
Globals: 0x400760 armed, 0x400764 timer table of entries 0x18-0x23 (one of 4 variants).
"""

from romconv.core.errors import ConversionError
from romconv.games.marble.sim import marble_sim as ms
from romconv.games.marble.sim.marble_sim import s8, s16

PISTONS = tuple(0x401650 + 0x10 * k for k in range(36))
TYPE = 0x29
_AERIAL = 3
ARMED = 0x400760
TIMERS = 0x400764
_ARM_ZONES = (4, 5)
UP = 0xffff                       # timer word while the piston is up
_ANIM = 0x21342
TIMER0 = 0x242c6                  # 0x186ac: first timers of entries 0-0x17
TIMER = 0x242f6                   # 0x1844a: reload timers of entries 0-0x17
VARIANT0, VARIANT = 0x243e6, 0x243f6   # timer tables of entries 0x18-0x23 (arm, reload)
CELL_X, CELL_Y, TOP = 0x24406, 0x2442a, 0x2444e
FIXED = 0x18                      # entries below use the fixed tables
_BUCKETS = (0xc, 0x18)             # 0x1844a sound groups: entries < 0xc, < 0x18, the rest
_REFLECT = 12                      # 0x1881c: below top + 12 px the piston is a wall
_LAUNCH_VZ, _LAUNCH_DZ, _LAUNCH_DV = 0x70000, 0xc0000, 0x6000


def _coords_18972(m, e):
    """Screen position +0xc of entry e from its cell (* 8) and top (0x400690/2 written)."""
    x = (s8(m.rb(e + 4)) << 3) & 0xffff; y = (s8(m.rb(e + 5)) << 3) & 0xffff
    m.ww(0x400690, x); m.ww(0x400692, y)
    sx = (y - x + 0x88) & 0xffff
    sy = (m.rw(0x40097e) + m.rw(e + 6) + 0x54 - ((s16(x) + s16(y)) >> 1)) & 0xffff
    m.wl(e + 0xc, sx << 16 | sy)


def _timer_word(m, k, fixed, variant_ptr):
    """Timer of entry k: table word k (k < 0x18), else word k - 0x18 at the variant table."""
    return m.rom_w(fixed + 2 * k) if k < FIXED else m.rw(variant_ptr + 2 * (k - FIXED))


def arm_186ac(m):
    """Aerial race, from the zone hook 0x13966: a player live in zone 4/5 arms the table
    (variant rng(4), every entry's timer, cell, top; 0x1bb28 leaves the last cell in the
    scratch 0x400690-0x4006a2), none tears it down (pistons up leave the draw list)."""
    if m.rw(0x400394) != _AERIAL: return
    armed = False
    for k in range(m.rw(0x400396)):
        a = ms.P + 0xe2 * k
        if m.rb(a + 0x18) == 1 and m.rb(a + 0x1b) in _ARM_ZONES: armed = True
    if not m.rb(ARMED) and armed:
        v = ms.rng_13a98(m, 4)
        m.wl(TIMERS, m.rom_l(VARIANT0 + 4 * v))
        for k, e in enumerate(PISTONS):
            m.wb(e, k)
            m.ww(e + 2, _timer_word(m, k, TIMER0, m.rl(TIMERS)))
            m.wb(e + 4, m.prog[CELL_X + k]); m.wb(e + 5, m.prog[CELL_Y + k])
            m.ww(0x400690, (s8(m.rb(e + 4)) << 3) & 0xffff)     # 0x1bb28
            m.ww(0x400692, (s8(m.rb(e + 5)) << 3) & 0xffff)
            ms.derive_1bb50(m)
            m.ww(e + 6, m.rom_w(TOP + 2 * k))
        m.wl(TIMERS, m.rom_l(VARIANT + 4 * v))
        m.wb(ARMED, 1)
        m.events.append(("piston arm", v))
    elif m.rb(ARMED) and not armed:
        for e in PISTONS:
            if m.rw(e + 2) == UP and m.drawlist: m.drawlist.remove(m, TYPE, m.rb(e))
            m.ww(e + 6, 0); m.wb(e + 5, 0); m.wb(e + 4, 0); m.ww(e + 2, 0)
        m.wb(ARMED, 0)
        m.events.append(("piston teardown",))


def update_1844a(m):
    """Armed aerial race, per entry: timer down; at 0 up (anim 0x21342, draw list insert); up:
    anim step, at its end down (draw list removal, timer reload); up: 0x18972. Then one
    rng(2) per sound group that got a piston up (sounds not modelled)."""
    if m.rw(0x400394) != _AERIAL or not m.rb(ARMED): return
    rose = [False, False, False]
    for k, e in enumerate(PISTONS):
        t = m.rw(e + 2)
        if s16(t) >= 0:
            t = (t - 1) & 0xffff
            m.ww(e + 2, t)
            if s16(t) <= 0:
                m.wl(e + 8, _ANIM); m.ww(e + 2, UP)
                if m.drawlist: m.drawlist.insert(m, TYPE, m.rb(e))
                rose[0 if k < _BUCKETS[0] else (1 if k < _BUCKETS[1] else 2)] = True
                m.events.append(("piston up", k))
        elif t == UP:
            m.wl(e + 8, (m.rl(e + 8) + 4) & 0xffffffff)
            if m.rl(m.rl(e + 8)) == 0xffffffff:
                if m.drawlist: m.drawlist.remove(m, TYPE, m.rb(e))
                m.ww(e + 2, _timer_word(m, m.rb(e), TIMER, m.rl(TIMERS)))
                m.events.append(("piston down", k))
        if m.rw(e + 2) == UP: _coords_18972(m, e)
    for r in rose:
        if r: ms.rng_13a98(m, 2)


def contact_1881c(m, a2):
    """Player a2 in the armed aerial race against every piston up on its new cell
    (0x400697/9): back to the old position; old cell the piston's and z word on its top:
    launch (vz, z + 12 px, v halved +- 0x6000, sign by rng(2) and a d0 high word, airborne); lower than top + 12:
    v reversed. 1 if any piston matched (the caller refreshes the cell)."""
    if m.rw(0x400394) != _AERIAL or not m.rb(ARMED): return 0
    nx, ny = m.rb(0x400697), m.rb(0x400699)
    ox, oy = (m.sl(0x400684) >> 19) & 0xff, (m.sl(0x400688) >> 19) & 0xff
    hit = 0
    for e in PISTONS:
        if m.rw(e + 2) != UP or m.rb(e + 4) != nx or m.rb(e + 5) != ny: continue
        m.wl(a2 + 0xc, m.rl(0x400684)); m.wl(a2 + 0x10, m.rl(0x400688))
        if m.rb(e + 4) == ox and m.rb(e + 5) == oy and m.rw(e + 6) == m.rw(a2 + 0x14):
            m.wl(a2 + 8, _LAUNCH_VZ)
            m.wl(a2 + 0x14, (m.rl(a2 + 0x14) + _LAUNCH_DZ) & 0xffffffff)
            # 0x13a98 writes only the low word of d0 and 0x1881c tests the long: the high
            # word left from before decides too (first call 0xffff: always plus, second:
            # the new vx high word)
            hi = 0xffff
            for o in (0, 4):
                d = _LAUNCH_DV if ms.rng_13a98(m, 2) or hi else -_LAUNCH_DV
                m.wl(a2 + o, ((m.sl(a2 + o) >> 1) + d) & 0xffffffff)
                hi = m.rw(a2 + o)
            m.wb(a2 + 0x36, 2)
            m.sounds.append(0x45)
            m.events.append(("piston launch", e))
        elif s16(m.rw(a2 + 0x14)) - s16(m.rw(e + 6)) < _REFLECT:
            m.wl(a2, -m.sl(a2) & 0xffffffff); m.wl(a2 + 4, -m.sl(a2 + 4) & 0xffffffff)
            m.events.append(("piston bounce", e))
        hit = 1
    return hit


def enable(m):
    """Pistons on: arm/teardown in the zone hook, 0x1844a in main_tick, 0x1881c."""
    m.pistons = True


# ---------------------------------------------------------------- trace checks


def piston_mos(m, i):
    from romconv.games.marble.sim import marble_enemy, marble_mo
    return marble_mo.other_mos(m, TYPE, i) if (TYPE, i) in marble_enemy.drawn(m) else []


# ---------------------------------------------------------------- fixture


def _piston_obj(m, i):
    """Piston fixture check values: timer, cell x/y, top, anim cursor, screen x/y, the drawn
    frame header (rl(+8), 0 if not in the draw list), its MOs."""
    from romconv.games.marble.sim import marble_enemy
    a = PISTONS[i]
    shown = (TYPE, i) in marble_enemy.drawn(m)
    return (m.rw(a + 2), m.rb(a + 4), m.rb(a + 5), m.sw(a + 6), m.rl(a + 8), m.sw(a + 0xc),
            m.sw(a + 0xe), m.rom_l(m.rl(a + 8)) if shown else 0, tuple(piston_mos(m, i)))


def tick_state(m):
    """Fixture values of one tick: player, armed flag, timer table, 36 pistons, draw list,
    cell scratch 0x400690-0x4006a2."""
    from romconv.games.marble.sim import marble_enemy
    return (ms.death_state(m), m.rb(ARMED), m.rl(TIMERS)) + \
        tuple(_piston_obj(m, i) for i in range(36)) + \
        (tuple(marble_enemy.drawn(m)), tuple(m.rw(a) for a in range(0x400690, 0x4006a4, 2)))


_PW, _NW, _SW = 43, 10, 10   # ints per player, per piston, scratch words in a fixture tick


def load_psfixture(path):
    """Segments of source/test/test_marble_pistons/marble_pistons_trace.h (make_fixture.py
    pswrite): [{"race", "frame", "ram", "ticks": [(c6, c7, camera, frames, tick_state)]}]."""
    import re
    text = open(path, encoding="utf-8").read()
    num = lambda s: [int(v, 0) for v in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", s)]
    arrays = {name: body for name, body in
              re.findall(r"static const \w+ (\w+)\[\] = \{(.*?)\n?\};", text, re.S)}
    segs = []
    for race, frame, nram, ramname, nticks, tickname, nmos, moname, nlist, listname in re.findall(
            r"\{ *(\d+), *(\d+), *(\d+), *(\w+), *(\d+), *(\w+), *(\d+), *(\w+), *(\d+), *(\w+) *\}",
            arrays["marble_piston_segments"]):
        ram = [(int(addr, 0), bytes(num(arrays[data]))) for addr, _, data in
               re.findall(r"\{ *(0x[0-9a-f]+), *(\d+), *(\w+) *\}", arrays[ramname])]
        mw = num(arrays[moname]); lw = num(arrays[listname])
        mos = [tuple(mw[k:k + 3]) for k in range(0, len(mw), 3)]
        pairs = [tuple(lw[k:k + 2]) for k in range(0, len(lw), 2)]
        v = num(arrays[tickname]); w = 4 + _PW + 2 + 36 * _NW + 2 + _SW
        assert len(v) == w * int(nticks)
        ticks = []
        for k in range(0, len(v), w):
            t = v[k:k + w]
            c6, c7, cam, frames = t[:4]
            p = t[4:4 + _PW]
            player = tuple(p[:10]) + (bytes(p[10:40]), p[40], tuple(mos[p[42]:p[42] + p[41]]))
            armed, timers = t[4 + _PW], t[5 + _PW] & 0xffffffff
            objs = []
            for j in range(36):
                q = t[6 + _PW + _NW * j:6 + _PW + _NW * (j + 1)]
                objs.append(tuple(q[:7]) + (q[7] & 0xffffffff, tuple(mos[q[9]:q[9] + q[8]])))
            r = 6 + _PW + 36 * _NW
            n, first = t[r:r + 2]
            scratch = tuple(x & 0xffff for x in t[r + 2:r + 2 + _SW])
            ticks.append((c6, c7, cam, frames, (player, armed, timers) + tuple(objs) +
                          (tuple(pairs[first:first + n]), scratch)))
        segs.append({"race": int(race), "frame": int(frame), "ram": ram, "ticks": ticks})
    return segs


def fixture_state(st):
    """tick_state with the anim cursor long and timers long unsigned (fixture ints)."""
    player, armed, timers = st[:3]
    objs = tuple(o[:4] + (o[4] & 0xffffffff,) + o[5:7] + (o[7] & 0xffffffff, o[8])
                 for o in st[3:39])
    return (player, armed, timers & 0xffffffff) + objs + st[39:]


def sweep(m, race):
    """Race sweep with all modelled enemies (marble_enemy.setup(m) done) on a cleared RAM:
    the remake race start, then main_tick + sort per camera step of marble_mo.race_sweep,
    player idle (zone of the start: the table stays unarmed). Type 0x29 entries iff up.
    Returns per tick (armed, draw list)."""
    from romconv.games.marble.sim import marble_enemy, marble_mo
    ms.begin_race(m, race)
    top, rng = m.rom_w(m.t.desc + 0x10), m.rom_w(m.t.desc + 0x12)
    cams = list(range(top, top + rng + 1, marble_mo.SWEEP_STEP))
    cams = cams + cams[::-1] + cams
    out = []
    for k, cam in enumerate(cams):
        ms.main_tick(m, 0, 0, cam & 0xffffffff, k + 1)
        marble_mo.boxes_and_sort(m)
        lst = marble_enemy.drawn(m)
        if m.rb(ARMED) and race != _AERIAL:
            raise ConversionError("race %d cam %d: 0x400760 set" % (race, cam))
        for i, a in enumerate(PISTONS):
            if ((TYPE, i) in lst) != (m.rb(ARMED) != 0 and m.rw(a + 2) == UP):
                raise ConversionError("race %d cam %d: piston %d timer %04x, draw list %s"
                                 % (race, cam, i, m.rw(a + 2), lst))
        out.append((m.rb(ARMED), tuple(lst)))
    return out
