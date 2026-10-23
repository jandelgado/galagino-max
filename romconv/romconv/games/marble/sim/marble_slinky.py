"""Marble Madness slinky model (Phase 4b-2): green worms 0x401302 (4 x 0x60, draw type 4).
1:1 ports named by ROM address from Marble Love (state-sub-14c46.ts, sub-14966.ts,
helper-15148.ts, state-dispatch-15460.ts, state-sub-15670.ts, nearest-neighbor.ts,
sprite-coords-jsr-150d0.ts, script-slot-bbox-test-14e92.ts) on the work RAM image of
marble_sim.Mem. Facts: MARBLE.md "Enemies".

Fields: v +0/+4, pos +0xc/+0x10/+0x14, +0x18 live, +0x19 index, +0x1a state, +0x1b zone,
+0x1c/+0x20 marble velocity at the catch, +0x24 tick, +0x25 ticks per frame, +0x26 frame
step, +0x27, +0x28 screen (x word, y word), +0x2c masks (5 x 3 words), +0x4a next waypoint,
+0x4e waypoint list (4 byte records x, y, next, pad), +0x52/+0x54 band, +0x56 prey (index
into 0x1eff6), +0x58 frame cursor, +0x5c anim list.

States: 0 walk the waypoints, 1 chase the prey, 2 pushed by the marble, 3 back to the path,
4 stopped (-> 1), 5 ate the marble (-> 3), 6 lost the prey (-> 3).

0x14a28-0x14bba (states 1/5/6, not in Marble Love) is ported from the disassembly (user
ruling 2026-10-12, this block only).
"""

from romconv.core.errors import ConversionError
from romconv.games.marble.sim import marble_sim as ms
from romconv.games.marble.sim.marble_sim import s8, s16, s32

SLINKIES = tuple(0x401302 + 0x60 * k for k in range(4))   # = table 0x1f006
SPAWN_TABLE = 0x2257a                  # per race: 8 byte entries lo, hi, waypoints, zone, pad
_OBJ_PTRS = 0x1eff6                     # P1, P2, steelie 0, steelie 1
_WALK, _CHASE, _PUSHED, _BACK, _STOPPED, _ATE, _LOST = range(7)

_ANIM_IDLE, _ANIM_RIGHT, _ANIM_DOWN, _ANIM_UP, _ANIM_LEFT = 0x20c18, 0x20c48, 0x20c6c, 0x20c90, 0x20cb4
_ANIM_CHASE, _ANIM_PUSHED, _ANIM_LOST, _ANIM_ATE = 0x20cd8, 0x20d64, 0x20d6c, 0x20e28
_ANIM_STOP = (0x20dd8, 0x20e14, 0x20dec, 0x20e00)          # +x, -x, +y, -y
_EAT_FRAMES = 0x20faa, 0x20fb6          # player +0x5a at the catch: rolling, dizzy


def _cell(v): return s16(s32(v) >> 19)


def _octant(a, b):
    """|a|, |b| words: major + 3/8 minor."""
    if a > b: return ((b >> 3) * 3 + a) & 0xffff
    return ((a >> 3) * 3 + b) & 0xffff


def _wp(m, p):
    """Waypoint record at p: cell x, y, next offset (records)."""
    return s8(m.prog[p]), s8(m.prog[p + 1]), s8(m.prog[p + 2])


# ---------------------------------------------------------------- spawn, screen, masks


def _coords_150d0(m, a2):
    """Screen position +0x28 (x word, y word) as slot objects, then masks 0x264aa mode 2."""
    x, y = m.rw(a2 + 0xc), m.rw(a2 + 0x10)
    m.ww(0x400690, x); m.ww(0x400692, y)
    sx = (y - x + 0x88) & 0xffff
    sy = (m.rw(0x40097e) + m.rw(a2 + 0x14) + 0x54 - ((s16(x) + s16(y)) >> 1)) & 0xffff
    m.wl(a2 + 0x28, sx << 16 | sy)
    ms.occlusion_264aa(m, a2, 2)


def _init_14c46(m, a2, e):
    """Slinky at the first waypoint of entry e, walking, idle anim, in the draw list."""
    pts = m.rom_l(e + 2)
    m.wl(a2 + 0x4a, pts); m.wl(a2 + 0x4e, pts); m.wb(a2 + 0x1b, m.prog[e + 6])
    m.ww(a2 + 0x52, s8(m.prog[e]) & 0xffff); m.ww(a2 + 0x54, s8(m.prog[e + 1]) & 0xffff)
    m.wl(a2 + 0xc, ((s8(m.prog[pts]) << 19) + 0x40000) & 0xffffffff)
    m.wl(a2 + 0x10, ((s8(m.prog[pts + 1]) << 19) + 0x40000) & 0xffffffff)
    ground_1bb08(m, a2)
    m.wl(a2 + 4, 0); m.wl(a2, 0)
    m.wb(a2 + 0x18, 1); m.wb(a2 + 0x1a, _WALK); m.wl(a2 + 0x28, 0)
    for i in range(5): m.ww(a2 + 0x2c + 6 * i, 0)
    m.wl(a2 + 0x5c, _ANIM_IDLE); m.wl(a2 + 0x58, _ANIM_IDLE)
    m.wb(a2 + 0x26, 1); m.wb(a2 + 0x24, 0); m.wb(a2 + 0x25, 2)
    _coords_150d0(m, a2)
    if m.drawlist: m.drawlist.insert(m, 4, m.rb(a2 + 0x19))


def height(m, a2):
    """0x1bb08 + 0x1cc62(1): cell scratch of a2's x/y, vertex fetch, height under a2."""
    m.ww(0x400690, m.rw(a2 + 0xc)); m.ww(0x400692, m.rw(a2 + 0x10))
    ms.derive_1bb50(m)
    ms.terrain_1caba(m)
    return ms.height_1cc62(m) & 0xffffffff


def ground_1bb08(m, a2):
    m.wl(a2 + 0x14, height(m, a2))


def _divs(n, d):
    """divs.w quotient word; overflow leaves the numerator's low word (68000 V set)."""
    q = int(n / d)
    return s16(q) if -0x8000 <= q <= 0x7fff else s16(n & 0xffff)


def spawn_14c46(m, old, new):
    """Camera band step (0x144e4): spawn a slinky for every entry of 0x2257a[race] whose lo
    or hi band is entered from outside [lo, hi] and whose waypoint list no live slinky has
    (free slot = highest free index of 0x1f006); then remove slinkies whose range is left."""
    old = s8(old); new = s8(new)
    e = m.rom_l(SPAWN_TABLE + 4 * m.rw(0x400394))
    while m.prog[e] != 0xff:
        free = [a for a in SLINKIES if not m.rb(a + 0x18)]
        if not free: break
        lo, hi = s8(m.prog[e]), s8(m.prog[e + 1])
        taken = any(m.rb(a + 0x18) and m.rl(a + 0x4e) == m.rom_l(e + 2) for a in SLINKIES)
        if not taken and (new & 0xff) in (m.prog[e], m.prog[e + 1]) and not lo <= old <= hi:
            _init_14c46(m, free[-1], e)
            m.events.append(("slinky spawn", free[-1]))
        e += 8
    for a in SLINKIES:
        if not m.rb(a + 0x18): continue
        lo, hi = m.sw(a + 0x52), m.sw(a + 0x54)
        if (old == lo and new < lo) or (old == hi and new > hi):
            m.wb(a + 0x18, 0); m.events.append(("slinky removed", a))
            if m.drawlist: m.drawlist.remove(m, 4, m.rb(a + 0x19))


# ---------------------------------------------------------------- per tick


def _anim_15460(m, a2):
    """Anim list +0x5c by state, frame cursor +0x58 to its start, tick reset."""
    st = m.rb(a2 + 0x1a)
    if st in (_WALK, _BACK):
        cx, cy = _cell(m.rl(a2 + 0xc)), _cell(m.rl(a2 + 0x10))
        tx, ty, _ = _wp(m, m.rl(a2 + 0x4a))
        dx = dy = 0
        def step(c, t): return -8 if c > t else (8 if c < t else 0)
        if m.sl(a2) == 0:
            dx = step(cx, tx)
            if not dx: dy = step(cy, ty)
        else:
            dy = step(cy, ty)
            if not dy: dx = step(cx, tx)
        anim = _ANIM_DOWN if dy > 0 else _ANIM_UP if dy < 0 else \
            _ANIM_RIGHT if dx > 0 else _ANIM_LEFT if dx < 0 else _ANIM_IDLE
        m.wl(a2 + 0x5c, anim)
        m.wl(a2, (dx << 16) & 0xffffffff); m.wl(a2 + 4, (dy << 16) & 0xffffffff)
        m.wb(a2 + 0x26, 1)
    elif st == _CHASE:
        m.wl(a2 + 0x5c, _ANIM_CHASE); m.wb(a2 + 0x27, 0); m.wb(a2 + 0x26, 1)
    elif st == _PUSHED:
        cur, prev = m.rl(a2 + 0x5c), m.rl(a2 + 0x58)
        if cur != _ANIM_PUSHED:
            if cur == prev:
                m.wl(a2 + 0x5c, _ANIM_PUSHED); m.wb(a2 + 0x26, 1)
            else:
                m.wb(a2 + 0x26, 0xff if 4 > s16(s32(prev - cur) >> 2) else 1)
    elif st == _STOPPED:
        vx, vy = m.sl(a2 + 0x1c), m.sl(a2 + 0x20)
        if abs(vx) > abs(vy): anim = _ANIM_STOP[0] if vx > 0 else _ANIM_STOP[1]
        else: anim = _ANIM_STOP[2] if vy > 0 else _ANIM_STOP[3]
        m.wl(a2 + 0x5c, anim); m.wb(a2 + 0x26, 1)
    elif st == _ATE:
        m.wl(a2 + 0x5c, _ANIM_ATE)
    elif st == _LOST:
        m.wl(a2 + 0x5c, _ANIM_LOST)
    m.wl(a2 + 0x58, m.rl(a2 + 0x5c)); m.wb(a2 + 0x24, 0)
    m.wb(a2 + 0x25, 2 if m.rb(a2 + 0x1a) in (_WALK, _STOPPED) else 1)


def _prey_15670(m, a2):
    """Walking slinky picks a rolling player in its zone (moving, on the ground, not taken by
    another chasing slinky) as prey +0x56; starts the chase at octant distance 0x180-0x280."""
    n = m.rw(0x400396); d2 = n & 0xff; a1 = 0
    for i in range(n):
        o = 0x400018 + 0xe2 * i
        if m.rb(o + 0x18) != 1 or m.rb(o + 0x1b) != m.rb(a2 + 0x1b): continue
        if abs(m.sl(o)) + abs(m.sl(o + 4)) <= 0xc000: continue
        if m.rb(o + 0x36) == 2 or m.rb(o + 0x1a) not in (0, 1, 5): continue
        key = s8(m.rb(o + 0x19)) & 0xffff
        if any(s != a2 and m.rb(s + 0x18) == 1 and m.rb(s + 0x1a) == _CHASE
               and m.rw(s + 0x56) == key for s in SLINKIES):
            continue
        d2 = (d2 - 1) & 0xff; a1 = o
    if (s8(d2) & 0xffff) == n: return
    if n == 2 and d2 == 0: m.fail("two player prey choice 0x15fe6")
    d = _octant(abs(s32(m.rl(a1 + 0xc) - m.rl(a2 + 0xc))) >> 12 & 0xffff,
               abs(s32(m.rl(a1 + 0x10) - m.rl(a2 + 0x10))) >> 12 & 0xffff)
    m.ww(a2 + 0x56, s8(m.rb(a1 + 0x19)) & 0xffff)
    if not 0x180 < d < 0x280: return
    m.wb(a2 + 0x1a, _CHASE)
    m.events.append(("slinky chase", a2))
    _anim_15460(m, a2)


def _nearest_14dec(m, a2):
    """Next waypoint +0x4a = nearest record of the list (octant of cell deltas x16)."""
    cx, cy = _cell(m.rl(a2 + 0xc)), _cell(m.rl(a2 + 0x10))
    p = m.rl(a2 + 0x4e); best, bp = 0x400, 0xffffffff
    while m.prog[p] != 0xff and m.prog[p + 1] != 0xff:
        x, y, _ = _wp(m, p)
        dx = (abs(s16(cx - x)) << 4) & 0xffff; dy = (abs(s16(cy - y)) << 4) & 0xffff
        d = _octant(dx, dy)
        if d < best: best, bp = d, p
        p += 4
    m.wl(a2 + 0x4a, bp)


def _state_15148(m, a2):
    """Slinky state machine."""
    st = m.rb(a2 + 0x1a)
    if st in (_WALK, _BACK): return _walk_15196(m, a2)
    if st == _CHASE: return _chase_15316(m, a2)
    if st == _PUSHED: return _pushed_1527c(m, a2)
    if st == _STOPPED:
        m.wb(a2 + 0x1a, _CHASE); _anim_15460(m, a2); return
    if st in (_ATE, _LOST):
        m.wl(a2 + 0xc, (m.rl(a2 + 0xc) & 0xfff80000) + 0x40000 & 0xffffffff)
        m.wl(a2 + 0x10, (m.rl(a2 + 0x10) & 0xfff80000) + 0x40000 & 0xffffffff)
        if st == _ATE:
            o = m.rom_l(_OBJ_PTRS + 4 * m.sw(a2 + 0x56))
            ms.sound_15884(m); m.wb(o + 0x57, 0x65); ms.enter_25bae(m, o, 4)
            m.events.append(("slinky ate", a2))
        m.wb(a2 + 0x1a, _BACK)
        _nearest_14dec(m, a2)
        _anim_15460(m, a2)


def _walk_15196(m, a2):
    """States 0/3: on the list's first waypoint stop and head for the next; on the next
    waypoint advance; then re-aim (0x15460) and, walking, look for prey."""
    cx, cy = _cell(m.rl(a2 + 0xc)), _cell(m.rl(a2 + 0x10))
    base = m.rl(a2 + 0x4e)
    x, y, _ = _wp(m, base)
    if (cx, cy) == (x, y) and m.rl(a2 + 0x5c) != _ANIM_IDLE:
        if m.rb(a2 + 0x1a) == _BACK: m.wb(a2 + 0x1a, _WALK)
        m.wl(a2 + 0x5c, _ANIM_IDLE); m.wl(a2 + 4, 0); m.wl(a2, 0)
        m.wb(a2 + 0x26, 1); m.wl(a2 + 0x58, _ANIM_IDLE); m.wb(a2 + 0x24, 0); m.wb(a2 + 0x25, 2)
        _, _, nxt = _wp(m, m.rl(a2 + 0x4a))
        m.wl(a2 + 0x4a, (nxt * 4 + base) & 0xffffffff)
    else:
        x, y, nxt = _wp(m, m.rl(a2 + 0x4a))
        if (cx, cy) == (x, y):
            if m.rb(a2 + 0x1a) == _BACK: m.wb(a2 + 0x1a, _WALK)
            m.wl(a2 + 0x4a, (nxt * 4 + base) & 0xffffffff)
        _anim_15460(m, a2)
    if m.rb(a2 + 0x1a) == _WALK: _prey_15670(m, a2)


def _chase_15316(m, a2):
    """State 1: prey within 0x70 (octant of 1/16 px deltas): the slinky eats it (prey state
    7 hidden at the slinky, slinky 5); within 0xc0 the prey is pulled towards the slinky;
    either way, or out of reach (> 0x100 on an axis), the slinky goes to state 6, except
    after a catch."""
    o = m.rom_l(_OBJ_PTRS + 4 * m.sw(a2 + 0x56))
    dx = s32(m.rl(o + 0xc) - m.rl(a2 + 0xc)); dy = s32(m.rl(o + 0x10) - m.rl(a2 + 0x10))
    ax = (abs(dx) >> 12) & 0xffff; ay = (abs(dy) >> 12) & 0xffff
    if ax > 0x100 or ay > 0x100:
        m.wb(a2 + 0x1a, _LOST); _anim_15460(m, a2); return
    d = _octant(ax, ay)
    if d < _CATCH:
        m.wl(o + 0xc, m.rl(a2 + 0xc)); m.wl(o + 0x10, m.rl(a2 + 0x10))
        m.wb(o + 0x1a, 7); m.wb(a2 + 0x1a, _ATE)
        m.sounds.append(0x33); m.events.append(("slinky catch", a2))
        _anim_15460(m, a2); return
    if d < 0xc0 and d:
        qx = _divs(dx, d); qy = _divs(dy, d)
        m.wl(o, (0x400 * qx >> 4) & 0xffffffff); m.wl(o + 4, (0x400 * qy >> 4) & 0xffffffff)
    m.wb(a2 + 0x1a, _LOST); _anim_15460(m, a2)


_CATCH = 0x70     # TS `dist1 > 0x70` contradicts its own disassembly comment (ble skips)


def _pushed_1527c(m, a2):
    """State 2: carried by the marble's push with friction; stops (4) when still, or when
    the step would change its height (up: 1, down: 4) the step is undone."""
    ms.friction_25e7c(m, a2, 2)
    if m.sl(a2) == 0 and m.sl(a2 + 4) == 0:
        m.wb(a2 + 0x1a, _STOPPED); _anim_15460(m, a2); return
    x, y, z = m.rl(a2 + 0xc), m.rl(a2 + 0x10), m.rl(a2 + 0x14)
    m.wl(a2 + 0xc, (x + m.rl(a2)) & 0xffffffff); m.wl(a2 + 0x10, (y + m.rl(a2 + 4)) & 0xffffffff)
    ground_1bb08(m, a2)
    nz = m.rl(a2 + 0x14)
    if nz != z:
        m.wb(a2 + 0x1a, _STOPPED if s32(z) > s32(nz) else _CHASE)
        m.wl(a2 + 0xc, x); m.wl(a2 + 0x10, y); m.wl(a2 + 0x14, z)
    _anim_15460(m, a2)


def _tick_14966(m, a2):
    """Per slinky: frame ticker; at the end of a frame step the cursor (list end or no
    move: restart, walking slinkies take a step) and run the state machine; pushed slinkies
    run it every tick. Then screen position and masks."""
    if not m.rb(a2 + 0x18): return
    m.wb(a2 + 0x24, m.rb(a2 + 0x24) + 1)
    run = m.rb(a2 + 0x1a) == _PUSHED
    if not s8(m.rb(a2 + 0x25)) > s8(m.rb(a2 + 0x24)):
        m.wb(a2 + 0x24, 0)
        p = (m.rl(a2 + 0x58) + s8(m.rb(a2 + 0x26)) * 4) & 0xffffffff
        m.wl(a2 + 0x58, p)
        if m.rl(p) == 0xffffffff or p == m.rl(a2 + 0x5c):
            m.wl(a2 + 0x58, m.rl(a2 + 0x5c))
            if s8(m.rb(a2 + 0x26)) > 0 and m.rb(a2 + 0x1a) in (_WALK, _BACK):
                m.wl(a2 + 0xc, (m.rl(a2 + 0xc) + m.rl(a2)) & 0xffffffff)
                m.wl(a2 + 0x10, (m.rl(a2 + 0x10) + m.rl(a2 + 4)) & 0xffffffff)
            run = True
    if run: _state_15148(m, a2)
    if m.rb(a2 + 0x1a) in (_CHASE, _ATE, _LOST): _lunge_14a28(m, a2)
    _coords_150d0(m, a2)


REAR = 0x2255a                         # z lift (px) per lunge tick 6..0x25


def _lunge_14a28(m, a2):
    """States 1/5/6, lunge ticks 6..0x25 (+0x27): the slinky rears up (z = ground + table)
    and, while its prey stands on the same ground in its zone, homes in on it
    (v += (aim - v) >> 5, aim 0x80 per octant unit); a step onto other ground is undone."""
    c = s8(m.rb(a2 + 0x27))
    if 5 < c < 0x26:
        d5 = height(m, a2)
        m.wl(a2 + 0x14, ((s8(m.prog[REAR + c - 6]) << 16) + d5) & 0xffffffff)
        o = m.rom_l(_OBJ_PTRS + 4 * m.sw(a2 + 0x56))
        if m.rb(o + 0x18) == 1 and m.rb(o + 0x1b) == m.rb(a2 + 0x1b) and not m.rb(o + 0x36) \
                and m.rl(o + 0x14) == d5 and m.rb(o + 0x1a) in (0, 1, 5, 7):
            _home(m, a2, o)
        else:
            m.wl(a2, 0); m.wl(a2 + 4, 0)
        m.wl(0x400684, m.rl(a2 + 0xc)); m.wl(0x400688, m.rl(a2 + 0x10))
        m.wl(a2 + 0xc, (m.rl(a2 + 0xc) + m.rl(a2)) & 0xffffffff)
        m.wl(a2 + 0x10, (m.rl(a2 + 0x10) + m.rl(a2 + 4)) & 0xffffffff)
        if height(m, a2) != d5:
            m.wl(a2 + 0xc, m.rl(0x400684)); m.wl(a2 + 0x10, m.rl(0x400688))
    m.wb(a2 + 0x27, m.rb(a2 + 0x27) + 1)


def _home(m, a2, o):
    dx = s32(m.rl(o + 0xc) - m.rl(a2 + 0xc)); dy = s32(m.rl(o + 0x10) - m.rl(a2 + 0x10))
    m.wl(a2, (dx >> 4) & 0xffffffff); m.wl(a2 + 4, (dy >> 4) & 0xffffffff)
    d = _octant((abs(dx) >> 12) & 0xffff, (abs(dy) >> 12) & 0xffff)
    qx, qy = (_divs(dx, d), _divs(dy, d)) if d else (0, 0)
    for off, q in ((0, qx), (4, qy)):
        v = m.sl(a2 + off); aim = s32(0x800 * q) >> 4
        m.wl(a2 + off, (v + (s32(aim - v) >> 5)) & 0xffffffff)


def update_1493c(m):
    for a in SLINKIES: _tick_14966(m, a)


# ---------------------------------------------------------------- contact with the marble


def _box(m, a2):
    """Slinky contact box offsets (x, y, w, h) from its frame header bytes 4-7."""
    p = m.rl(m.rl(a2 + 0x58))
    if p == 0xffffffff: return -4, -4, 8, 8
    return s8(m.prog[p + 4]), s8(m.prog[p + 5]), s8(m.prog[p + 6]), s8(m.prog[p + 7])


def overlap(a0, a1, b0, b1):
    return b0 <= a0 <= b1 or b0 <= a1 <= b1


def contact_14e92(m, a2):
    """Player a2 (scratch 0x400690 = new position) against the slinkies, races 1/2/5: the
    first hit slinky (walking: pushed with the marble's velocity) holds the marble at its
    old position, the player wobbles in state 5 (+0x56 = 50 ticks, eaten frames)."""
    if m.rw(0x400394) not in (1, 2, 5): return
    wx, wy, wz = m.sw(0x400690), m.sw(0x400692), m.sw(0x400694)
    for s in SLINKIES:
        if not m.rb(s + 0x18): continue
        bx, by, bw, bh = _box(m, s)
        x0 = s16(m.rw(s + 0xc) + bx); y0 = s16(m.rw(s + 0x10) + by); z0 = m.sw(s + 0x14)
        if not overlap(wz, s16(wz + 0xe), z0, s16(z0 + 0x10)): continue
        if not overlap(s16(wx - 3), s16(wx + 3), x0, s16(x0 + bw)): continue
        if not overlap(s16(wy - 3), s16(wy + 3), y0, s16(y0 + bh)): continue
        _hit(m, a2, s)
        return


def _hit(m, a2, s):
    if m.rb(s + 0x1a) in (_WALK, _BACK):
        m.wb(s + 0x1a, _PUSHED); _anim_15460(m, s)
        m.wl(s, m.rl(a2)); m.wl(s + 0x1c, m.rl(a2))
        m.wl(s + 4, m.rl(a2 + 4)); m.wl(s + 0x20, m.rl(a2 + 4))
        m.events.append(("slinky pushed", s))
    st = m.rb(s + 0x1a); key = s8(m.rb(a2 + 0x19)) & 0xffff
    if st in (_CHASE, _LOST) and key == m.rw(s + 0x56): return
    if m.rb(a2 + 0x1a) != 5 and st != _ATE: m.ww(s + 0x56, key)
    m.wl(a2 + 0xc, m.rl(0x400684)); m.wl(a2 + 0x10, m.rl(0x400688))
    m.wl(a2 + 4, 0); m.wl(a2, 0)
    pst = m.rb(a2 + 0x1a)
    if pst != 5:
        if pst != 1: m.sounds.append(0x39)
        m.wb(a2 + 0x5f, 0); m.wb(a2 + 0x60, 2)
        m.wl(a2 + 0x5a, _EAT_FRAMES[pst == 1])
    if m.rb(a2 + 0x1a) not in (5, 7):
        m.wb(a2 + 0x1a, 5); m.wb(a2 + 0x56, 0x32)
        m.events.append(("eaten wobble", a2))


def enable(m):
    """Slinkies on: band spawns, 0x1493c in main_tick, 0x14e92, player states 5/7."""
    m.slinkies = True


# ---------------------------------------------------------------- free run (tick start traces)


def setup(m):
    """main_tick model with deaths, slots, sprites, draw list, steelies and slinkies."""
    from romconv.games.marble.sim import marble_enemy
    return marble_enemy.setup(m)


def slinky_mos(m, i):
    from romconv.games.marble.sim import marble_enemy, marble_mo
    return marble_mo.other_mos(m, 4, i) if (4, i) in marble_enemy.drawn(m) else []


# ---------------------------------------------------------------- fixture


def _slinky_obj(m, i):
    """Slinky fixture check values: +0x18, +0x1a, x, y, z, sx, sy (+0x28/+0x2a), ticker
    +0x24-+0x27, next waypoint +0x4a, prey +0x56, frame cursor +0x58, the drawn frame header
    (rl(+0x58), 0 if not in the draw list), masks +0x2c (30 bytes), its MOs."""
    from romconv.games.marble.sim import marble_enemy
    a = SLINKIES[i]; o = a - 0x400000
    shown = m.rb(a + 0x18) and (4, i) in marble_enemy.drawn(m)
    return (m.rb(a + 0x18), m.rb(a + 0x1a), m.sl(a + 0xc), m.sl(a + 0x10), m.sl(a + 0x14),
            m.sw(a + 0x28), m.sw(a + 0x2a), m.rl(a + 0x24), m.rl(a + 0x4a), m.rw(a + 0x56),
            m.rl(a + 0x58), m.rl(m.rl(a + 0x58)) if shown else 0,
            bytes(m.rb(a + 0x2c + k) for k in range(30)), tuple(slinky_mos(m, i)))


def tick_state(m):
    """Fixture values of one tick: player, both steelies, the four slinkies, the draw list."""
    from romconv.games.marble.sim import marble_enemy
    return (ms.death_state(m), marble_enemy.steelie_obj(m, 0), marble_enemy.steelie_obj(m, 1)) \
        + tuple(_slinky_obj(m, i) for i in range(4)) + (tuple(marble_enemy.drawn(m)),)


def load_slfixture(path):
    """Segments of source/test/test_marble_slinkies/marble_slinkies_trace.h (make_fixture.py
    slwrite): [{"race", "frame", "ram", "ticks": [(c6, c7, camera, frames, tick_state)]}]."""
    import re
    text = open(path, encoding="utf-8").read()
    num = lambda s: [int(v, 0) for v in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", s)]
    arrays = {name: body for name, body in
              re.findall(r"static const \w+ (\w+)\[\] = \{(.*?)\n?\};", text, re.S)}
    segs = []
    for race, frame, nram, ramname, nticks, tickname, nmos, moname, nlist, listname in re.findall(
            r"\{ *(\d+), *(\d+), *(\d+), *(\w+), *(\d+), *(\w+), *(\d+), *(\w+), *(\d+), *(\w+) *\}",
            arrays["marble_slinkies_segments"]):
        ram = [(int(addr, 0), bytes(num(arrays[data]))) for addr, _, data in
               re.findall(r"\{ *(0x[0-9a-f]+), *(\d+), *(\w+) *\}", arrays[ramname])]
        mw = num(arrays[moname]); lw = num(arrays[listname])
        mos = [tuple(mw[k:k + 3]) for k in range(0, len(mw), 3)]
        pairs = [tuple(lw[k:k + 2]) for k in range(0, len(lw), 2)]
        PW, SW, LW = 43, 45, 44
        v = num(arrays[tickname]); w = 4 + PW + 2 * SW + 4 * LW + 2
        assert len(v) == w * int(nticks)
        ticks = []
        for k in range(0, len(v), w):
            t = v[k:k + w]
            c6, c7, cam, frames = t[:4]
            p = t[4:4 + PW]
            player = tuple(p[:10]) + (bytes(p[10:40]), p[40], tuple(mos[p[42]:p[42] + p[41]]))
            objs = []
            for j in range(2):
                q = t[4 + PW + SW * j:4 + PW + SW * (j + 1)]
                objs.append(tuple(q[:13]) + (bytes(q[13:43]), tuple(mos[q[44]:q[44] + q[43]])))
            b = 4 + PW + 2 * SW
            for j in range(4):
                q = t[b + LW * j:b + LW * (j + 1)]
                objs.append(tuple(q[:12]) + (bytes(q[12:42]), tuple(mos[q[43]:q[43] + q[42]])))
            n, first = t[-2:]
            ticks.append((c6, c7, cam, frames, (player,) + tuple(objs) +
                          (tuple(pairs[first:first + n]),)))
        segs.append({"race": int(race), "frame": int(frame), "ram": ram, "ticks": ticks})
    return segs


def sweep(m, race):
    """Race sweep with steelies and slinkies (setup(m) done) on a cleared RAM: the remake race
    start, then main_tick + 0x26f3e sort per camera step of marble_mo.race_sweep, player
    idle. Checks every slinky spawn against its table bands and the draw list. Returns (per
    tick (live slinkies, draw list), spawns, removals)."""
    from romconv.games.marble.sim import marble_enemy, marble_mo
    ms.begin_race(m, race)
    top, rng = m.rom_w(m.t.desc + 0x10), m.rom_w(m.t.desc + 0x12)
    cams = list(range(top, top + rng + 1, marble_mo.SWEEP_STEP))
    if race == 4: cams.reverse()
    cams = cams + cams[::-1] + cams
    out, spawns, removals = [], 0, 0
    for k, cam in enumerate(cams):
        before = [m.rb(a + 0x18) for a in SLINKIES]
        ms.main_tick(m, 0, 0, cam & 0xffffffff, k + 1)
        marble_mo.boxes_and_sort(m)
        lst = marble_enemy.drawn(m)
        for i, a in enumerate(SLINKIES):
            live = m.rb(a + 0x18)
            if not before[i] and live:
                spawns += 1
                band = s16(cam - top) >> 4
                if band not in (m.sw(a + 0x52), m.sw(a + 0x54)):
                    raise ConversionError("race %d cam %d: slinky %d spawned at band %d" % (race, cam, i, band))
            if before[i] and not live: removals += 1
            if bool(live) != ((4, i) in lst):
                raise ConversionError("race %d cam %d: slinky %d live %d, draw list %s" % (race, cam, i, live, lst))
        out.append((tuple(m.rb(a + 0x18) for a in SLINKIES), tuple(lst)))
    return out, spawns, removals
