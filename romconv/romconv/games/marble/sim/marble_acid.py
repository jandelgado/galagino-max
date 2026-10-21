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
"""

from romconv.core.errors import ConversionError
from romconv.games.marble.sim import marble_sim as ms
from romconv.games.marble.sim.marble_sim import s8, s16, s32

ACIDS = tuple(0x401482 + 0x42 * k for k in range(7))   # = table 0x1f07a
SPAWN_TABLE = 0x23d4a                  # per race: 8 byte entries lo, hi, path, zone, pad
SINGLE = 0x23d62, 0x23d66              # random path lists (rng(2)): one acid (zone 5) ...
GROUP = 0x23d6a, 0x23d82               # ... or six
_KIND_SINGLE = 5
_ANIM_DOWN, _ANIM_UP, _ANIM_RIGHT, _ANIM_LEFT = 0x20ec4, 0x20ee4, 0x20ea4, 0x20f24
HIT_TICKS = 0x1c                       # +0x25 after dissolving a marble: frame held 28 ticks
TYPE = 0xe


def _cell(v): return s16(s32(v) >> 19)


# ---------------------------------------------------------------- spawn, path, screen


def _coords_1778e(m, a2):
    """Screen position +0x28 (x word, y word) as slot objects (no masks)."""
    x, y = m.rw(a2 + 0xc), m.rw(a2 + 0x10)
    m.ww(0x400690, x); m.ww(0x400692, y)
    sx = (y - x + 0x88) & 0xffff
    sy = (m.rw(0x40097e) + m.rw(a2 + 0x14) + 0x54 - ((s16(x) + s16(y)) >> 1)) & 0xffff
    m.wl(a2 + 0x28, sx << 16 | sy)


def _aim_1d242(m, a2):
    """One axis step of 8 px towards the path cursor's cell (y first while moving in x),
    crawl anim; ticks per frame 2, or 1 while a player rolls in the acid's zone 6."""
    cx, cy = _cell(m.rl(a2 + 0xc)), _cell(m.rl(a2 + 0x10))
    p = m.rl(a2 + 0x2c)
    tx, ty = s8(m.rb(p)), s8(m.rb(p + 1))
    def step(c, t): return -8 if c > t else (8 if c < t else 0)
    dx = dy = 0
    if m.rl(a2):
        dy = step(cy, ty)
        if not dy: dx = step(cx, tx)
    else:
        dx = step(cx, tx)
        if not dx: dy = step(cy, ty)
    anim = _ANIM_DOWN if dy > 0 else _ANIM_UP if dy < 0 else _ANIM_RIGHT if dx > 0 else _ANIM_LEFT
    m.wl(a2, (dx << 16) & 0xffffffff); m.wl(a2 + 4, (dy << 16) & 0xffffffff)
    m.wl(a2 + 0x3e, anim); m.wl(a2 + 0x3a, anim)
    m.wb(a2 + 0x24, 0); m.wb(a2 + 0x25, 2)
    for i in range(m.rw(0x400396)):
        o = 0x400018 + 0xe2 * i
        if m.rb(o + 0x18) == 1 and m.rb(a2 + 0x1b) == m.rb(o + 0x1b) == 6:
            m.wb(a2 + 0x25, 1); return


def _path_1d1ec(m, a2):
    """On the cursor's cell: cursor = path + next * 4. Then re-aim (0x1d242)."""
    p = m.rl(a2 + 0x2c)
    if (_cell(m.rl(a2 + 0xc)), _cell(m.rl(a2 + 0x10))) == (s8(m.rb(p)), s8(m.rb(p + 1))):
        m.wl(a2 + 0x2c, (m.rl(a2 + 0x30) + s8(m.rb(p + 2)) * 4) & 0xffffffff)
    _aim_1d242(m, a2)


def _init_17346(m, a2, e, path):
    """Acid at the first cell of path (corner, no centre offset), in the draw list."""
    m.wl(a2 + 0x2c, path); m.wl(a2 + 0x30, path); m.wb(a2 + 0x1b, m.prog[e + 6])
    m.ww(a2 + 0x34, s8(m.prog[e]) & 0xffff); m.ww(a2 + 0x36, s8(m.prog[e + 1]) & 0xffff)
    m.wl(a2 + 0xc, (s8(m.rb(path)) << 19) & 0xffffffff)
    m.wl(a2 + 0x10, (s8(m.rb(path + 1)) << 19) & 0xffffffff)
    from romconv.games.marble.sim import marble_slinky
    marble_slinky.ground_1bb08(m, a2)
    m.wl(a2 + 4, 0); m.wl(a2, 0)
    m.wb(a2 + 0x18, 1); m.wb(a2 + 0x1a, 0); m.wl(a2 + 0x28, 0)
    _path_1d1ec(m, a2)
    _coords_1778e(m, a2)
    if m.drawlist: m.drawlist.insert(m, TYPE, m.rb(a2 + 0x19))
    m.events.append(("acid spawn", a2))


def _free_slot(m):
    free = [a for a in ACIDS if not m.rb(a + 0x18)]
    return free[-1] if free else None


def spawn_17346(m, old, new):
    """Camera band step (0x144e4): per entry of 0x23d4a[race] not yet live (+0x30 == entry
    long, 0x1730c) whose lo or hi band is entered from outside [lo, hi]: long 0 picks a random
    path list (rng(2)) of one or six acids, else the entry's path; no free slot ends the walk.
    Then remove acid whose range is left."""
    old = s8(old); new = s8(new)
    e = m.rom_l(SPAWN_TABLE + 4 * m.rw(0x400394))
    while m.prog[e] != 0xff:
        lo, hi, path = s8(m.prog[e]), s8(m.prog[e + 1]), m.rom_l(e + 2)
        taken = any(m.rb(a + 0x18) and m.rl(a + 0x30) == path for a in ACIDS)
        if taken or (new & 0xff) not in (m.prog[e], m.prog[e + 1]) or lo <= old <= hi:
            e += 8; continue
        if path:
            paths = [path]
        else:
            single = m.prog[e + 6] == _KIND_SINGLE
            t = (SINGLE if single else GROUP)[ms.rng_13a98(m, 2) != 0]
            paths = [m.rom_l(t + 4 * k) for k in range(1 if single else 6)]
        full = False
        for p in paths:
            a = _free_slot(m)
            if a is None: full = True; break
            _init_17346(m, a, e, p)
        if full: break
        e += 8
    for a in ACIDS:
        if not m.rb(a + 0x18): continue
        lo, hi = m.sw(a + 0x34), m.sw(a + 0x36)
        if (old == lo and new < lo) or (old == hi and new > hi):
            m.wb(a + 0x18, 0); m.events.append(("acid removed", a))
            if m.drawlist: m.drawlist.remove(m, TYPE, m.rb(a + 0x19))


# ---------------------------------------------------------------- per tick


def _tick_1725a(m, a2):
    """Frame ticker; at the anim list end the acid steps by v and follows its path."""
    if not m.rb(a2 + 0x18): return
    m.wb(a2 + 0x24, m.rb(a2 + 0x24) + 1)
    if s8(m.rb(a2 + 0x25)) > s8(m.rb(a2 + 0x24)): return _coords_1778e(m, a2)
    m.wb(a2 + 0x24, 0)
    if s8(m.rb(a2 + 0x25)) > 2: m.wb(a2 + 0x25, 1)
    p = (m.rl(a2 + 0x3a) + 4) & 0xffffffff
    m.wl(a2 + 0x3a, p)
    if m.rl(p) == 0xffffffff:
        m.wl(a2 + 0x3a, m.rl(a2 + 0x3e))
        m.wl(a2 + 0xc, (m.rl(a2 + 0xc) + m.rl(a2)) & 0xffffffff)
        m.wl(a2 + 0x10, (m.rl(a2 + 0x10) + m.rl(a2 + 4)) & 0xffffffff)
        _path_1d1ec(m, a2)
    _coords_1778e(m, a2)


def update_17230(m):
    for a in ACIDS: _tick_1725a(m, a)


# ---------------------------------------------------------------- contact with the marble


def _box(m, a2):
    """Acid contact box offsets (x, y, w, h) from its frame header bytes 4-7."""
    p = m.rl(m.rl(a2 + 0x3a))
    if p == 0xffffffff: return -2, -2, 12, 12
    return s8(m.prog[p + 4]), s8(m.prog[p + 5]), s8(m.prog[p + 6]), s8(m.prog[p + 7])


def contact_175c8(m, a2):
    """Player a2 (scratch 0x400690 = new position) against the acid, races 2/5, no height
    test: the first hit dissolves the marble (state 9, +0x58 = acid number), the acid holds
    its frame. True on a hit (the caller then runs 0x1bab2)."""
    if m.rw(0x400394) not in (2, 5): return False
    from romconv.games.marble.sim import marble_slinky
    ov = marble_slinky.overlap
    wx, wy = m.sw(0x400690), m.sw(0x400692)
    for s in ACIDS:
        if not m.rb(s + 0x18): continue
        bx, by, bw, bh = _box(m, s)
        x0 = s16(m.rw(s + 0xc) + bx); y0 = s16(m.rw(s + 0x10) + by)
        if not ov(s16(wx - 3), s16(wx + 3), x0, s16(x0 + bw)): continue
        if not ov(s16(wy - 3), s16(wy + 3), y0, s16(y0 + bh)): continue
        ms.enter_25bae(m, a2, 9)
        m.sounds.append(0x5e)
        m.wb(a2 + 0x58, m.rb(s + 0x19)); m.wb(s + 0x25, HIT_TICKS)
        m.events.append(("acid hit", s))
        return True
    return False


def _target_176d2(m, a2):
    """Dissolving marble (state 9) slides 1 px per tick on x and y towards the centre of its
    acid's contact box; positions keep whole pixels."""
    s = 0x401482 + 0x42 * s8(m.rb(a2 + 0x58))
    bx, by, bw, bh = _box(m, s)
    tx = s16((bw >> 1) + bx + m.rw(s + 0xc)); ty = s16((bh >> 1) + by + m.rw(s + 0x10))
    x, y = m.sw(a2 + 0xc), m.sw(a2 + 0x10)
    def step(c, t): return -1 if c > t else (1 if c < t else 0)
    m.wl(a2 + 0xc, ((x + step(x, tx)) & 0xffff) << 16)
    m.wl(a2 + 0x10, ((y + step(y, ty)) & 0xffff) << 16)


def dissolve_2584e(m, a2):
    """Player state 9: slide into the acid, dissolve anim (end: lost), screen, visibility."""
    _target_176d2(m, a2)
    ms.anim_25fc2(m, a2)
    ms.proj_1b9cc(m, a2, True)
    ms.enter_1281c(m, a2)


def enable(m):
    """Acid on: band spawns, 0x17230 in main_tick, 0x175c8, player state 9."""
    m.acid = True


# ---------------------------------------------------------------- free run (tick start traces)


def acid_mos(m, i):
    from romconv.games.marble.sim import marble_enemy, marble_mo
    return marble_mo.other_mos(m, TYPE, i) if (TYPE, i) in marble_enemy.drawn(m) else []


# ---------------------------------------------------------------- fixture


def _acid_obj(m, i):
    """Acid fixture check values: +0x18, x, y, z, sx, sy (+0x28/+0x2a), ticker +0x24/+0x25,
    path cursor +0x2c, frame cursor +0x3a, the drawn frame header (rl(+0x3a), 0 if not in
    the draw list), its MOs."""
    from romconv.games.marble.sim import marble_enemy
    a = ACIDS[i]
    shown = m.rb(a + 0x18) and (TYPE, i) in marble_enemy.drawn(m)
    return (m.rb(a + 0x18), m.sl(a + 0xc), m.sl(a + 0x10), m.sl(a + 0x14), m.sw(a + 0x28),
            m.sw(a + 0x2a), m.rw(a + 0x24), m.rl(a + 0x2c), m.rl(a + 0x3a),
            m.rl(m.rl(a + 0x3a)) if shown else 0, tuple(acid_mos(m, i)))


def tick_state(m):
    """Fixture values of one tick: player, the seven acids, the draw list."""
    from romconv.games.marble.sim import marble_enemy
    return (ms.death_state(m),) + tuple(_acid_obj(m, i) for i in range(7)) + \
        (tuple(marble_enemy.drawn(m)),)


_PW, _AW = 43, 12                        # ints per player, per acid in a fixture tick


def load_acfixture(path):
    """Segments of source/test/test_marble_acid/marble_acid_trace.h (make_fixture.py acwrite):
    [{"race", "frame", "ram", "ticks": [(c6, c7, camera, frames, tick_state)]}]."""
    import re
    text = open(path).read()
    num = lambda s: [int(v, 0) for v in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", s)]
    arrays = {name: body for name, body in
              re.findall(r"static const \w+ (\w+)\[\] = \{(.*?)\n?\};", text, re.S)}
    segs = []
    for race, frame, nram, ramname, nticks, tickname, nmos, moname, nlist, listname in re.findall(
            r"\{ *(\d+), *(\d+), *(\d+), *(\w+), *(\d+), *(\w+), *(\d+), *(\w+), *(\d+), *(\w+) *\}",
            arrays["marble_acid_segments"]):
        ram = [(int(addr, 0), bytes(num(arrays[data]))) for addr, _, data in
               re.findall(r"\{ *(0x[0-9a-f]+), *(\d+), *(\w+) *\}", arrays[ramname])]
        mw = num(arrays[moname]); lw = num(arrays[listname])
        mos = [tuple(mw[k:k + 3]) for k in range(0, len(mw), 3)]
        pairs = [tuple(lw[k:k + 2]) for k in range(0, len(lw), 2)]
        v = num(arrays[tickname]); w = 4 + _PW + 7 * _AW + 2
        assert len(v) == w * int(nticks)
        ticks = []
        for k in range(0, len(v), w):
            t = v[k:k + w]
            c6, c7, cam, frames = t[:4]
            p = t[4:4 + _PW]
            player = tuple(p[:10]) + (bytes(p[10:40]), p[40], tuple(mos[p[42]:p[42] + p[41]]))
            objs = []
            for j in range(7):
                q = t[4 + _PW + _AW * j:4 + _PW + _AW * (j + 1)]
                objs.append(tuple(q[:10]) + (tuple(mos[q[11]:q[11] + q[10]]),))
            n, first = t[-2:]
            ticks.append((c6, c7, cam, frames, (player,) + tuple(objs) +
                          (tuple(pairs[first:first + n]),)))
        segs.append({"race": int(race), "frame": int(frame), "ram": ram, "ticks": ticks})
    return segs


def sweep(m, race):
    """Race sweep with all modelled enemies (marble_enemy.setup(m) done) on a cleared RAM: the
    remake race start, then main_tick + 0x26f3e sort per camera step of marble_mo.race_sweep,
    player idle. Checks every acid spawn against its table bands and the draw list. Returns
    (per tick (live acid, draw list), spawns, removals)."""
    from romconv.games.marble.sim import marble_enemy, marble_mo
    ms.begin_race(m, race)
    top, rng = m.rom_w(m.t.desc + 0x10), m.rom_w(m.t.desc + 0x12)
    cams = list(range(top, top + rng + 1, marble_mo.SWEEP_STEP))
    if race == 4: cams.reverse()
    cams = cams + cams[::-1] + cams
    out, spawns, removals = [], 0, 0
    for k, cam in enumerate(cams):
        before = [m.rb(a + 0x18) for a in ACIDS]
        ms.main_tick(m, 0, 0, cam & 0xffffffff, k + 1)
        marble_mo.boxes_and_sort(m)
        lst = marble_enemy.drawn(m)
        for i, a in enumerate(ACIDS):
            live = m.rb(a + 0x18)
            if not before[i] and live:
                spawns += 1
                band = s16(cam - top) >> 4
                if band not in (m.sw(a + 0x34), m.sw(a + 0x36)):
                    raise ConversionError("race %d cam %d: acid %d spawned at band %d" % (race, cam, i, band))
            if before[i] and not live: removals += 1
            if bool(live) != ((TYPE, i) in lst):
                raise ConversionError("race %d cam %d: acid %d live %d, draw list %s" % (race, cam, i, live, lst))
        out.append((tuple(m.rb(a + 0x18) for a in ACIDS), tuple(lst)))
    return out, spawns, removals
