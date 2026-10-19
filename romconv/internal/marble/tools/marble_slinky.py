#!/usr/bin/env python3
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

  marble_slinky.py spawncheck TRACE RACE   model spawner 0x14c46 over the trace camera ==
                                           the slinky slots of the next snapshot
  marble_slinky.py slinky TRACE RACE       per tick from each snapshot: 0x1493c == next
                                           snapshot's slinky records (states 0/2/3/4)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import marble_sim as ms
from marble_sim import s8, s16, s32, rom_l, PROG

SLINKIES = tuple(0x401302 + 0x60 * k for k in range(4))   # = table 0x1f006
SLINKY_SIZE = 0x60
SPAWN_TABLE = 0x2257a                  # per race: 8 byte entries lo, hi, waypoints, zone, pad
OBJ_PTRS = 0x1eff6                     # P1, P2, steelie 0, steelie 1
WALK, CHASE, PUSHED, BACK, STOPPED, ATE, LOST = range(7)

ANIM_IDLE, ANIM_RIGHT, ANIM_DOWN, ANIM_UP, ANIM_LEFT = 0x20c18, 0x20c48, 0x20c6c, 0x20c90, 0x20cb4
ANIM_CHASE, ANIM_PUSHED, ANIM_LOST, ANIM_ATE = 0x20cd8, 0x20d64, 0x20d6c, 0x20e28
ANIM_STOP = (0x20dd8, 0x20e14, 0x20dec, 0x20e00)          # +x, -x, +y, -y
EAT_FRAMES = 0x20faa, 0x20fb6          # player +0x5a at the catch: rolling, dizzy


def cell(v): return s16(s32(v) >> 19)


def octant(a, b):
    """|a|, |b| words: major + 3/8 minor."""
    if a > b: return ((b >> 3) * 3 + a) & 0xffff
    return ((a >> 3) * 3 + b) & 0xffff


def wp(m, p):
    """Waypoint record at p: cell x, y, next offset (records)."""
    return s8(PROG[p]), s8(PROG[p + 1]), s8(PROG[p + 2])


# ---------------------------------------------------------------- spawn, screen, masks

def coords_150d0(m, a2):
    """Screen position +0x28 (x word, y word) as slot objects, then masks 0x264aa mode 2."""
    x, y = m.rw(a2 + 0xc), m.rw(a2 + 0x10)
    m.ww(0x400690, x); m.ww(0x400692, y)
    sx = (y - x + 0x88) & 0xffff
    sy = (m.rw(0x40097e) + m.rw(a2 + 0x14) + 0x54 - ((s16(x) + s16(y)) >> 1)) & 0xffff
    m.wl(a2 + 0x28, sx << 16 | sy)
    ms.occlusion_264aa(m, a2, 2)


def init_14c46(m, a2, e):
    """Slinky at the first waypoint of entry e, walking, idle anim, in the draw list."""
    pts = rom_l(e + 2)
    m.wl(a2 + 0x4a, pts); m.wl(a2 + 0x4e, pts); m.wb(a2 + 0x1b, PROG[e + 6])
    m.ww(a2 + 0x52, s8(PROG[e]) & 0xffff); m.ww(a2 + 0x54, s8(PROG[e + 1]) & 0xffff)
    m.wl(a2 + 0xc, ((s8(PROG[pts]) << 19) + 0x40000) & 0xffffffff)
    m.wl(a2 + 0x10, ((s8(PROG[pts + 1]) << 19) + 0x40000) & 0xffffffff)
    ground_1bb08(m, a2)
    m.wl(a2 + 4, 0); m.wl(a2, 0)
    m.wb(a2 + 0x18, 1); m.wb(a2 + 0x1a, WALK); m.wl(a2 + 0x28, 0)
    for i in range(5): m.ww(a2 + 0x2c + 6 * i, 0)
    m.wl(a2 + 0x5c, ANIM_IDLE); m.wl(a2 + 0x58, ANIM_IDLE)
    m.wb(a2 + 0x26, 1); m.wb(a2 + 0x24, 0); m.wb(a2 + 0x25, 2)
    coords_150d0(m, a2)
    if m.drawlist: m.drawlist.insert(m, 4, m.rb(a2 + 0x19))


def height(m, a2):
    """0x1bb08 + 0x1cc62(1): cell scratch of a2's x/y, vertex fetch, height under a2."""
    m.ww(0x400690, m.rw(a2 + 0xc)); m.ww(0x400692, m.rw(a2 + 0x10))
    ms.derive_1bb50(m)
    ms.terrain_1caba(m)
    return ms.height_1cc62(m) & 0xffffffff


def ground_1bb08(m, a2):
    m.wl(a2 + 0x14, height(m, a2))


def divs(n, d):
    """divs.w quotient word; overflow leaves the numerator's low word (68000 V set)."""
    q = int(n / d)
    return s16(q) if -0x8000 <= q <= 0x7fff else s16(n & 0xffff)


def spawn_14c46(m, old, new):
    """Camera band step (0x144e4): spawn a slinky for every entry of 0x2257a[race] whose lo
    or hi band is entered from outside [lo, hi] and whose waypoint list no live slinky has
    (free slot = highest free index of 0x1f006); then remove slinkies whose range is left."""
    old = s8(old); new = s8(new)
    e = rom_l(SPAWN_TABLE + 4 * m.rw(0x400394))
    while PROG[e] != 0xff:
        free = [a for a in SLINKIES if not m.rb(a + 0x18)]
        if not free: break
        lo, hi = s8(PROG[e]), s8(PROG[e + 1])
        taken = any(m.rb(a + 0x18) and m.rl(a + 0x4e) == rom_l(e + 2) for a in SLINKIES)
        if not taken and (new & 0xff) in (PROG[e], PROG[e + 1]) and not lo <= old <= hi:
            init_14c46(m, free[-1], e)
            m.events.append(("slinky spawn", free[-1]))
        e += 8
    for a in SLINKIES:
        if not m.rb(a + 0x18): continue
        lo, hi = m.sw(a + 0x52), m.sw(a + 0x54)
        if (old == lo and new < lo) or (old == hi and new > hi):
            m.wb(a + 0x18, 0); m.events.append(("slinky removed", a))
            if m.drawlist: m.drawlist.remove(m, 4, m.rb(a + 0x19))


# ---------------------------------------------------------------- per tick

def anim_15460(m, a2):
    """Anim list +0x5c by state, frame cursor +0x58 to its start, tick reset."""
    st = m.rb(a2 + 0x1a)
    if st in (WALK, BACK):
        cx, cy = cell(m.rl(a2 + 0xc)), cell(m.rl(a2 + 0x10))
        tx, ty, _ = wp(m, m.rl(a2 + 0x4a))
        dx = dy = 0
        def step(c, t): return -8 if c > t else (8 if c < t else 0)
        if m.sl(a2) == 0:
            dx = step(cx, tx)
            if not dx: dy = step(cy, ty)
        else:
            dy = step(cy, ty)
            if not dy: dx = step(cx, tx)
        anim = ANIM_DOWN if dy > 0 else ANIM_UP if dy < 0 else \
            ANIM_RIGHT if dx > 0 else ANIM_LEFT if dx < 0 else ANIM_IDLE
        m.wl(a2 + 0x5c, anim)
        m.wl(a2, (dx << 16) & 0xffffffff); m.wl(a2 + 4, (dy << 16) & 0xffffffff)
        m.wb(a2 + 0x26, 1)
    elif st == CHASE:
        m.wl(a2 + 0x5c, ANIM_CHASE); m.wb(a2 + 0x27, 0); m.wb(a2 + 0x26, 1)
    elif st == PUSHED:
        cur, prev = m.rl(a2 + 0x5c), m.rl(a2 + 0x58)
        if cur != ANIM_PUSHED:
            if cur == prev:
                m.wl(a2 + 0x5c, ANIM_PUSHED); m.wb(a2 + 0x26, 1)
            else:
                m.wb(a2 + 0x26, 0xff if 4 > s16(s32(prev - cur) >> 2) else 1)
    elif st == STOPPED:
        vx, vy = m.sl(a2 + 0x1c), m.sl(a2 + 0x20)
        if abs(vx) > abs(vy): anim = ANIM_STOP[0] if vx > 0 else ANIM_STOP[1]
        else: anim = ANIM_STOP[2] if vy > 0 else ANIM_STOP[3]
        m.wl(a2 + 0x5c, anim); m.wb(a2 + 0x26, 1)
    elif st == ATE:
        m.wl(a2 + 0x5c, ANIM_ATE)
    elif st == LOST:
        m.wl(a2 + 0x5c, ANIM_LOST)
    m.wl(a2 + 0x58, m.rl(a2 + 0x5c)); m.wb(a2 + 0x24, 0)
    m.wb(a2 + 0x25, 2 if m.rb(a2 + 0x1a) in (WALK, STOPPED) else 1)


def prey_15670(m, a2):
    """Walking slinky picks a rolling player in its zone (moving, on the ground, not taken by
    another chasing slinky) as prey +0x56; starts the chase at octant distance 0x180-0x280."""
    n = m.rw(0x400396); d2 = n & 0xff; a1 = 0
    for i in range(n):
        o = 0x400018 + 0xe2 * i
        if m.rb(o + 0x18) != 1 or m.rb(o + 0x1b) != m.rb(a2 + 0x1b): continue
        if abs(m.sl(o)) + abs(m.sl(o + 4)) <= 0xc000: continue
        if m.rb(o + 0x36) == 2 or m.rb(o + 0x1a) not in (0, 1, 5): continue
        key = s8(m.rb(o + 0x19)) & 0xffff
        if any(s != a2 and m.rb(s + 0x18) == 1 and m.rb(s + 0x1a) == CHASE
               and m.rw(s + 0x56) == key for s in SLINKIES):
            continue
        d2 = (d2 - 1) & 0xff; a1 = o
    if (s8(d2) & 0xffff) == n: return
    if n == 2 and d2 == 0: m.fail("two player prey choice 0x15fe6")
    d = octant(abs(s32(m.rl(a1 + 0xc) - m.rl(a2 + 0xc))) >> 12 & 0xffff,
               abs(s32(m.rl(a1 + 0x10) - m.rl(a2 + 0x10))) >> 12 & 0xffff)
    m.ww(a2 + 0x56, s8(m.rb(a1 + 0x19)) & 0xffff)
    if not 0x180 < d < 0x280: return
    m.wb(a2 + 0x1a, CHASE)
    m.events.append(("slinky chase", a2))
    anim_15460(m, a2)


def nearest_14dec(m, a2):
    """Next waypoint +0x4a = nearest record of the list (octant of cell deltas x16)."""
    cx, cy = cell(m.rl(a2 + 0xc)), cell(m.rl(a2 + 0x10))
    p = m.rl(a2 + 0x4e); best, bp = 0x400, 0xffffffff
    while PROG[p] != 0xff and PROG[p + 1] != 0xff:
        x, y, _ = wp(m, p)
        dx = (abs(s16(cx - x)) << 4) & 0xffff; dy = (abs(s16(cy - y)) << 4) & 0xffff
        d = octant(dx, dy)
        if d < best: best, bp = d, p
        p += 4
    m.wl(a2 + 0x4a, bp)


def state_15148(m, a2):
    """Slinky state machine."""
    st = m.rb(a2 + 0x1a)
    if st in (WALK, BACK): return walk_15196(m, a2)
    if st == CHASE: return chase_15316(m, a2)
    if st == PUSHED: return pushed_1527c(m, a2)
    if st == STOPPED:
        m.wb(a2 + 0x1a, CHASE); anim_15460(m, a2); return
    if st in (ATE, LOST):
        m.wl(a2 + 0xc, (m.rl(a2 + 0xc) & 0xfff80000) + 0x40000 & 0xffffffff)
        m.wl(a2 + 0x10, (m.rl(a2 + 0x10) & 0xfff80000) + 0x40000 & 0xffffffff)
        if st == ATE:
            o = rom_l(OBJ_PTRS + 4 * m.sw(a2 + 0x56))
            ms.sound_15884(m); m.wb(o + 0x57, 0x65); ms.enter_25bae(m, o, 4)
            m.events.append(("slinky ate", a2))
        m.wb(a2 + 0x1a, BACK)
        nearest_14dec(m, a2)
        anim_15460(m, a2)


def walk_15196(m, a2):
    """States 0/3: on the list's first waypoint stop and head for the next; on the next
    waypoint advance; then re-aim (0x15460) and, walking, look for prey."""
    cx, cy = cell(m.rl(a2 + 0xc)), cell(m.rl(a2 + 0x10))
    base = m.rl(a2 + 0x4e)
    x, y, _ = wp(m, base)
    if (cx, cy) == (x, y) and m.rl(a2 + 0x5c) != ANIM_IDLE:
        if m.rb(a2 + 0x1a) == BACK: m.wb(a2 + 0x1a, WALK)
        m.wl(a2 + 0x5c, ANIM_IDLE); m.wl(a2 + 4, 0); m.wl(a2, 0)
        m.wb(a2 + 0x26, 1); m.wl(a2 + 0x58, ANIM_IDLE); m.wb(a2 + 0x24, 0); m.wb(a2 + 0x25, 2)
        _, _, nxt = wp(m, m.rl(a2 + 0x4a))
        m.wl(a2 + 0x4a, (nxt * 4 + base) & 0xffffffff)
    else:
        x, y, nxt = wp(m, m.rl(a2 + 0x4a))
        if (cx, cy) == (x, y):
            if m.rb(a2 + 0x1a) == BACK: m.wb(a2 + 0x1a, WALK)
            m.wl(a2 + 0x4a, (nxt * 4 + base) & 0xffffffff)
        anim_15460(m, a2)
    if m.rb(a2 + 0x1a) == WALK: prey_15670(m, a2)


def chase_15316(m, a2):
    """State 1: prey within 0x70 (octant of 1/16 px deltas): the slinky eats it (prey state
    7 hidden at the slinky, slinky 5); within 0xc0 the prey is pulled towards the slinky;
    either way, or out of reach (> 0x100 on an axis), the slinky goes to state 6, except
    after a catch."""
    o = rom_l(OBJ_PTRS + 4 * m.sw(a2 + 0x56))
    dx = s32(m.rl(o + 0xc) - m.rl(a2 + 0xc)); dy = s32(m.rl(o + 0x10) - m.rl(a2 + 0x10))
    ax = (abs(dx) >> 12) & 0xffff; ay = (abs(dy) >> 12) & 0xffff
    if ax > 0x100 or ay > 0x100:
        m.wb(a2 + 0x1a, LOST); anim_15460(m, a2); return
    d = octant(ax, ay)
    if d < CATCH:
        m.wl(o + 0xc, m.rl(a2 + 0xc)); m.wl(o + 0x10, m.rl(a2 + 0x10))
        m.wb(o + 0x1a, 7); m.wb(a2 + 0x1a, ATE)
        m.sounds.append(0x33); m.events.append(("slinky catch", a2))
        anim_15460(m, a2); return
    if d < 0xc0 and d:
        qx = divs(dx, d); qy = divs(dy, d)
        m.wl(o, (0x400 * qx >> 4) & 0xffffffff); m.wl(o + 4, (0x400 * qy >> 4) & 0xffffffff)
    m.wb(a2 + 0x1a, LOST); anim_15460(m, a2)


CATCH = 0x70     # TS `dist1 > 0x70` contradicts its own disassembly comment (ble skips)


def pushed_1527c(m, a2):
    """State 2: carried by the marble's push with friction; stops (4) when still, or when
    the step would change its height (up: 1, down: 4) the step is undone."""
    ms.friction_25e7c(m, a2, 2)
    if m.sl(a2) == 0 and m.sl(a2 + 4) == 0:
        m.wb(a2 + 0x1a, STOPPED); anim_15460(m, a2); return
    x, y, z = m.rl(a2 + 0xc), m.rl(a2 + 0x10), m.rl(a2 + 0x14)
    m.wl(a2 + 0xc, (x + m.rl(a2)) & 0xffffffff); m.wl(a2 + 0x10, (y + m.rl(a2 + 4)) & 0xffffffff)
    ground_1bb08(m, a2)
    nz = m.rl(a2 + 0x14)
    if nz != z:
        m.wb(a2 + 0x1a, STOPPED if s32(z) > s32(nz) else CHASE)
        m.wl(a2 + 0xc, x); m.wl(a2 + 0x10, y); m.wl(a2 + 0x14, z)
    anim_15460(m, a2)


def tick_14966(m, a2):
    """Per slinky: frame ticker; at the end of a frame step the cursor (list end or no
    move: restart, walking slinkies take a step) and run the state machine; pushed slinkies
    run it every tick. Then screen position and masks."""
    if not m.rb(a2 + 0x18): return
    m.wb(a2 + 0x24, m.rb(a2 + 0x24) + 1)
    run = m.rb(a2 + 0x1a) == PUSHED
    if not s8(m.rb(a2 + 0x25)) > s8(m.rb(a2 + 0x24)):
        m.wb(a2 + 0x24, 0)
        p = (m.rl(a2 + 0x58) + s8(m.rb(a2 + 0x26)) * 4) & 0xffffffff
        m.wl(a2 + 0x58, p)
        if m.rl(p) == 0xffffffff or p == m.rl(a2 + 0x5c):
            m.wl(a2 + 0x58, m.rl(a2 + 0x5c))
            if s8(m.rb(a2 + 0x26)) > 0 and m.rb(a2 + 0x1a) in (WALK, BACK):
                m.wl(a2 + 0xc, (m.rl(a2 + 0xc) + m.rl(a2)) & 0xffffffff)
                m.wl(a2 + 0x10, (m.rl(a2 + 0x10) + m.rl(a2 + 4)) & 0xffffffff)
            run = True
    if run: state_15148(m, a2)
    if m.rb(a2 + 0x1a) in (CHASE, ATE, LOST): lunge_14a28(m, a2)
    coords_150d0(m, a2)


REAR = 0x2255a                         # z lift (px) per lunge tick 6..0x25


def lunge_14a28(m, a2):
    """States 1/5/6, lunge ticks 6..0x25 (+0x27): the slinky rears up (z = ground + table)
    and, while its prey stands on the same ground in its zone, homes in on it
    (v += (aim - v) >> 5, aim 0x80 per octant unit); a step onto other ground is undone."""
    c = s8(m.rb(a2 + 0x27))
    if 5 < c < 0x26:
        d5 = height(m, a2)
        m.wl(a2 + 0x14, ((s8(PROG[REAR + c - 6]) << 16) + d5) & 0xffffffff)
        o = rom_l(OBJ_PTRS + 4 * m.sw(a2 + 0x56))
        if m.rb(o + 0x18) == 1 and m.rb(o + 0x1b) == m.rb(a2 + 0x1b) and not m.rb(o + 0x36) \
                and m.rl(o + 0x14) == d5 and m.rb(o + 0x1a) in (0, 1, 5, 7):
            home(m, a2, o)
        else:
            m.wl(a2, 0); m.wl(a2 + 4, 0)
        m.wl(0x400684, m.rl(a2 + 0xc)); m.wl(0x400688, m.rl(a2 + 0x10))
        m.wl(a2 + 0xc, (m.rl(a2 + 0xc) + m.rl(a2)) & 0xffffffff)
        m.wl(a2 + 0x10, (m.rl(a2 + 0x10) + m.rl(a2 + 4)) & 0xffffffff)
        if height(m, a2) != d5:
            m.wl(a2 + 0xc, m.rl(0x400684)); m.wl(a2 + 0x10, m.rl(0x400688))
    m.wb(a2 + 0x27, m.rb(a2 + 0x27) + 1)


def home(m, a2, o):
    dx = s32(m.rl(o + 0xc) - m.rl(a2 + 0xc)); dy = s32(m.rl(o + 0x10) - m.rl(a2 + 0x10))
    m.wl(a2, (dx >> 4) & 0xffffffff); m.wl(a2 + 4, (dy >> 4) & 0xffffffff)
    d = octant((abs(dx) >> 12) & 0xffff, (abs(dy) >> 12) & 0xffff)
    qx, qy = (divs(dx, d), divs(dy, d)) if d else (0, 0)
    for off, q in ((0, qx), (4, qy)):
        v = m.sl(a2 + off); aim = s32(0x800 * q) >> 4
        m.wl(a2 + off, (v + (s32(aim - v) >> 5)) & 0xffffffff)


def update_1493c(m):
    for a in SLINKIES: tick_14966(m, a)


# ---------------------------------------------------------------- contact with the marble

def box(m, a2):
    """Slinky contact box offsets (x, y, w, h) from its frame header bytes 4-7."""
    p = m.rl(m.rl(a2 + 0x58))
    if p == 0xffffffff: return -4, -4, 8, 8
    return s8(PROG[p + 4]), s8(PROG[p + 5]), s8(PROG[p + 6]), s8(PROG[p + 7])


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
        bx, by, bw, bh = box(m, s)
        x0 = s16(m.rw(s + 0xc) + bx); y0 = s16(m.rw(s + 0x10) + by); z0 = m.sw(s + 0x14)
        if not overlap(wz, s16(wz + 0xe), z0, s16(z0 + 0x10)): continue
        if not overlap(s16(wx - 3), s16(wx + 3), x0, s16(x0 + bw)): continue
        if not overlap(s16(wy - 3), s16(wy + 3), y0, s16(y0 + bh)): continue
        hit(m, a2, s)
        return


def hit(m, a2, s):
    if m.rb(s + 0x1a) in (WALK, BACK):
        m.wb(s + 0x1a, PUSHED); anim_15460(m, s)
        m.wl(s, m.rl(a2)); m.wl(s + 0x1c, m.rl(a2))
        m.wl(s + 4, m.rl(a2 + 4)); m.wl(s + 0x20, m.rl(a2 + 4))
        m.events.append(("slinky pushed", s))
    st = m.rb(s + 0x1a); key = s8(m.rb(a2 + 0x19)) & 0xffff
    if st in (CHASE, LOST) and key == m.rw(s + 0x56): return
    if m.rb(a2 + 0x1a) != 5 and st != ATE: m.ww(s + 0x56, key)
    m.wl(a2 + 0xc, m.rl(0x400684)); m.wl(a2 + 0x10, m.rl(0x400688))
    m.wl(a2 + 4, 0); m.wl(a2, 0)
    pst = m.rb(a2 + 0x1a)
    if pst != 5:
        if pst != 1: m.sounds.append(0x39)
        m.wb(a2 + 0x5f, 0); m.wb(a2 + 0x60, 2)
        m.wl(a2 + 0x5a, EAT_FRAMES[pst == 1])
    if m.rb(a2 + 0x1a) not in (5, 7):
        m.wb(a2 + 0x1a, 5); m.wb(a2 + 0x56, 0x32)
        m.events.append(("eaten wobble", a2))


def enable(m):
    """Slinkies on: band spawns, 0x1493c in main_tick, 0x14e92, player states 5/7."""
    m.slinkies = True


# ---------------------------------------------------------------- trace checks

def cmd_spawncheck(trace, race):
    """Model spawner over consecutive snapshots' cameras vs the next snapshot's live slots
    and waypoint lists."""
    n = bad = ev = 0; t = ms.Terrain(race)
    for (_, r, _), (_, r2, _) in zip(trace, trace[1:]):
        m = ms.Mem(t, r)
        old = m.rw(0x40097e); new = ms.tw(r2, 0x40097e) & 0xffff
        top = m.sw(m.t.desc + 0x10)
        a = s16(old - top) >> 4; b = s16(new - top) >> 4
        if (a & 0xff) != (b & 0xff): spawn_14c46(m, a, b); ev += len(m.events)
        want = [(ms.tb(r2, s + 0x18) != 0, ms.tl(r2, s + 0x4e) if ms.tb(r2, s + 0x18) else 0)
                for s in SLINKIES]
        got = [(m.rb(s + 0x18) != 0, m.sl(s + 0x4e) if m.rb(s + 0x18) else 0) for s in SLINKIES]
        n += 1
        if want != got:
            bad += 1
            if bad <= 5: print("differ", n, want, got)
    print("spawncheck", n, "steps", ev, "spawns/removals", bad, "differ")
    return bad == 0


FIELDS = [(0, 4, "vx"), (4, 4, "vy"), (0xc, 4, "x"), (0x10, 4, "y"), (0x14, 4, "z"),
          (0x18, 1, "live"), (0x1a, 1, "state"), (0x1c, 8, "catch v"), (0x24, 4, "anim"),
          (0x28, 4, "screen"), (0x2c, 30, "masks"), (0x4a, 4, "next"), (0x56, 2, "prey"),
          (0x58, 8, "frame")]


def cmd_slinky(trace, race):
    """Snapshot pairs 2 frames apart (one tick), player rolling: 0x1493c on the first
    snapshot == the second snapshot's slinky records, slinkies in states 0/2/3/4 only."""
    t = ms.Terrain(race)
    n = bad = skip = 0; diffs = {}
    for (fr, r, _), (fr2, r2, _) in zip(trace, trace[1:]):
        if fr2 - fr != 2: continue
        live = [s for s in SLINKIES if ms.tb(r, s + 0x18) and ms.tb(r2, s + 0x18)]
        if not live: continue
        if any(ms.tb(r, s + 0x1a) in (1, 5, 6) or ms.tb(r2, s + 0x1a) in (1, 5, 6) for s in live) \
                or ms.tw(r, 0x40097e) != ms.tw(r2, 0x40097e):
            skip += 1; continue
        m = ms.Mem(t, r); m.sprites = True
        update_1493c(m)
        n += 1; ok = True
        for s in live:
            for off, size, name in FIELDS:
                a = s - 0x400000 + off
                if m.ram[a:a + size] != r2[a:a + size]:
                    ok = False; diffs[name] = diffs.get(name, 0) + 1
                    if bad < 8:
                        print("tick", fr, hex(s), name, m.ram[a:a + size].hex(), r2[a:a + size].hex(),
                              "state", ms.tb(r, s + 0x1a))
        if not ok: bad += 1
    print("slinky", n, "ticks", bad, "differ", skip, "skipped", diffs)
    return bad == 0


# ---------------------------------------------------------------- free run (tick start traces)

def setup(m):
    """main_tick model with deaths, slots, sprites, draw list, steelies and slinkies."""
    import marble_enemy
    return marble_enemy.setup(m)


def slinky_mos(m, i):
    import marble_enemy, marble_mo
    return marble_mo.other_mos(m, 4, i) if (4, i) in marble_enemy.drawn(m) else []


def ref_mos(r, v, typ, i):
    """MO words of object (typ, i) in the MO bank the snapshot shows built."""
    import marble_mo
    words, _ = marble_mo.mo_ram(r, v)
    k = 0
    for _, t, idx, mos in marble_mo.mo_list_26f3e(marble_mo.Ram(r)):
        if t == typ and idx == i: return words[k:k + len(mos)]
        k += len(mos)
    return []


def slinky_diff(m, r2, v2):
    """Player and steelies (marble_enemy.steelie_diff), the whole draw list in order (acid
    modelled since 4b-3), all four slinky records, their MOs."""
    import marble_enemy
    out = [d for d in marble_enemy.steelie_diff(m, r2, v2) if not d.startswith("draw list")]
    want = marble_enemy.drawn(ms.Mem(m.t, r2)); got = marble_enemy.drawn(m)
    if got != want: out.append("draw list %s/%s" % (got, want))
    for i, a in enumerate(SLINKIES):
        o = a - 0x400000
        out += ["l%d+%02x %02x/%02x" % (i, k, m.ram[o + k], r2[o + k])
                for k in range(SLINKY_SIZE) if m.ram[o + k] != r2[o + k]]
        if slinky_mos(m, i) != ref_mos(r2, v2, 4, i): out.append("slinky %d MOs" % i)
    return out


def segment(trace, race, start, end):
    """Free run from the first live snapshot after start to end."""
    import marble_mo
    t = ms.Terrain(race)
    i0 = next(k for k, x in enumerate(trace) if x[0] > start and ms.live_race(x[1]))
    m = setup(ms.Mem(t, trace[i0][1]))
    run = [x for x in trace[i0:] if x[0] <= end]
    n = 0; states = {}; ev = set()
    for (fr, r, v), (fr2, r2, v2) in zip(run, run[1:]):
        m.events = []; m.sounds = []
        ms.main_tick(m, *ms.death_inputs(r, r2))
        marble_mo.boxes_and_sort(m)
        n += 1
        for a in SLINKIES:
            if m.rb(a + 0x18): k = m.rb(a + 0x1a); states[k] = states.get(k, 0) + 1
        ev |= {e[0] for e in m.events if isinstance(e, tuple) and str(e[0]).startswith(("slinky", "eaten"))}
        d = slinky_diff(m, r2, v2)
        if d:
            print("slinkies race %d: tick %d (frame %d) differs: %s %s"
                  % (race, n, fr2, "; ".join(d[:12]), [e for e in m.events if e != "slot"][:8]))
            return False
    print("slinkies race %d: frames %d-%d, %d ticks equal, slinky ticks per state %s, events %s"
          % (race, run[0][0], run[-1][0], n, states, sorted(ev)))
    return True


def cmd_slinkies(trace, race, segs):
    ok = True
    for seg in segs:
        f = seg.split(":")
        ok &= segment(trace, race, int(f[0]), int(f[1]))
    return ok


# ---------------------------------------------------------------- fixture

def slinky_obj(m, i):
    """Slinky fixture check values: +0x18, +0x1a, x, y, z, sx, sy (+0x28/+0x2a), ticker
    +0x24-+0x27, next waypoint +0x4a, prey +0x56, frame cursor +0x58, the drawn frame header
    (rl(+0x58), 0 if not in the draw list), masks +0x2c (30 bytes), its MOs."""
    import marble_enemy
    a = SLINKIES[i]; o = a - 0x400000
    shown = m.rb(a + 0x18) and (4, i) in marble_enemy.drawn(m)
    return (m.rb(a + 0x18), m.rb(a + 0x1a), m.sl(a + 0xc), m.sl(a + 0x10), m.sl(a + 0x14),
            m.sw(a + 0x28), m.sw(a + 0x2a), m.rl(a + 0x24), m.rl(a + 0x4a), m.rw(a + 0x56),
            m.rl(a + 0x58), m.rl(m.rl(a + 0x58)) if shown else 0,
            bytes(m.rb(a + 0x2c + k) for k in range(30)), tuple(slinky_mos(m, i)))


def tick_state(m):
    """Fixture values of one tick: player, both steelies, the four slinkies, the draw list."""
    import marble_enemy
    return (ms.death_state(m), marble_enemy.steelie_obj(m, 0), marble_enemy.steelie_obj(m, 1)) \
        + tuple(slinky_obj(m, i) for i in range(4)) + (tuple(marble_enemy.drawn(m)),)


def load_slfixture(path):
    """Segments of source/test/test_marble_slinkies/marble_slinkies_trace.h (make_fixture.py
    slwrite): [{"race", "frame", "ram", "ticks": [(c6, c7, camera, frames, tick_state)]}]."""
    import re
    text = open(path).read()
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


def replay_slfixture(seg, m):
    """Run every slinky fixture tick on m (main_tick + 0x26f3e sort); index of the first tick
    that differs, or None."""
    import marble_mo
    setup(m)
    for k, (c6, c7, cam, frames, want) in enumerate(seg["ticks"]):
        ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
        marble_mo.boxes_and_sort(m)
        if tick_state(m) != want: return k
    return None


def cmd_slfixture(path):
    ok = True
    for seg in load_slfixture(path):
        bad = replay_slfixture(seg, ms.fixture_mem(seg, ms.Terrain(seg["race"])))
        print("slinkies fixture race %d frame %d: %d ticks, %s" % (seg["race"], seg["frame"],
              len(seg["ticks"]), "exact" if bad is None else "differs at tick %d" % bad))
        ok &= bad is None
    return ok


def sweep(m, race):
    """Race sweep with steelies and slinkies (setup(m) done) on a cleared RAM: the remake race
    start, then main_tick + 0x26f3e sort per camera step of marble_mo.race_sweep, player
    idle. Checks every slinky spawn against its table bands and the draw list. Returns (per
    tick (live slinkies, draw list), spawns, removals)."""
    import marble_enemy, marble_mo
    ms.begin_race(m, race)
    top, rng = ms.rom_w(m.t.desc + 0x10), ms.rom_w(m.t.desc + 0x12)
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
                    raise SystemExit("race %d cam %d: slinky %d spawned at band %d" % (race, cam, i, band))
            if before[i] and not live: removals += 1
            if bool(live) != ((4, i) in lst):
                raise SystemExit("race %d cam %d: slinky %d live %d, draw list %s" % (race, cam, i, live, lst))
        out.append((tuple(m.rb(a + 0x18) for a in SLINKIES), tuple(lst)))
    return out, spawns, removals


def cmd_sweep():
    for race in range(6):
        m = setup(ms.Mem(ms.Terrain(race), bytes(0x2000)))
        out, sp, rm = sweep(m, race)
        print("sweep race %d: %d ticks, %d spawns, %d removals, most live %d, longest list %d" % (
            race, len(out), sp, rm, max(sum(1 for v in o[0] if v) for o in out),
            max(len(o[1]) for o in out)))
    return True


def main(argv):
    cmd = argv[1]
    if cmd == "spawncheck": return 0 if cmd_spawncheck(ms.load_trace(argv[2]), int(argv[3])) else 1
    if cmd == "sweep": return 0 if cmd_sweep() else 1
    if cmd == "slfixture": return 0 if cmd_slfixture(argv[2]) else 1
    if cmd == "slinkies":
        return 0 if cmd_slinkies(ms.load_trace(argv[2]), int(argv[3]), argv[4:]) else 1
    if cmd == "slinky": return 0 if cmd_slinky(ms.load_trace(argv[2]), int(argv[3])) else 1
    print(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
