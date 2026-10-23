"""Marble Madness aerial race level objects (Phase 4c-2): 0x29cce tags 0x0a catapult,
0x0b/0x0d vacuum gates, 0x0c bounce boxes, and player state 0xa (sucked into a gate). 1:1
ports named by ROM address from Marble Love (sub-29cce.ts runCatapult0A, runGate0B/0D,
runBounce0C, refresh-frame-10fce.ts JT[10], slot-spawn-pattern-13d38.ts) on the work RAM
image of marble_sim.Mem. Facts: MARBLE.md "Aerial objects".
"""

from types import MappingProxyType

from romconv.games.marble.sim import marble_sim as ms
from romconv.games.marble.sim.marble_sim import s8, s16

_OLD_X, _OLD_Y, _OLD_Z = 0x400684, 0x400688, 0x40068c   # position before the move (longs)
_HIT_X, _HIT_Y = 0x400666, 0x400668                    # bounce flags of the 0x29cce epilogue
_POS_Z = 0x400694                                     # z word of the new position
_PLAYERS = 0x400396                                   # word: players in the game
_SLOT_LIST = 0x1f016                                  # slot addresses by slot number
_CATAPULT_SCRIPT = 0x1db80                            # arm throw script
_GATE_OPEN = MappingProxyType({0x0b: 0x22016, 0x0d: 0x220a6})           # slot +0x46 while a gate sucks
_GATE_CLOSE = MappingProxyType({0x0b: 0x1d752, 0x0d: 0x1d798})          # script restarted after a catch
_GATE_TOP = 0x3fc4                                    # z word: the gate frame is above
_PATTERN = 0x1ef32                                    # 0x13d38 star offsets (dx, dy bytes)
_STAR_CODE, _STARS, _HALF = 0x10b, 8, 4
_DIZZY_ANIM = 0x20faa


def _wdenom(dx, dy):
    """max + 3/8 min of |dx| * 16, |dy| * 16 (word): the 0x29cce distance measure."""
    ax, ay = (abs(s16(dx)) << 4) & 0xffff, (abs(s16(dy)) << 4) & 0xffff
    return (((ay >> 3) * 3 + ax) if ax > ay else ((ax >> 3) * 3 + ay)) & 0xffff


def _divs(n, d):
    """divs.w quotient (truncated toward 0, low word sign extended)."""
    q = abs(n) // abs(d)
    return s16(-q if (n < 0) != (d < 0) else q)


def _catapult_0a(m, a2, s, d6, a0):
    """0x29e22: on the arm (|d| < 8 px) with no vertical speed: while the arm moves the marble
    stays at its old position; else it is thrown: on the slot, z - 3 px, vz 10 px, vx
    rng(0x2000) - 0x1000, vy -0x25000 - rng(0xa000), airborne, state 3 (+0x58 0x0a, +0x59 15
    ticks), the slot runs the throw script."""
    if not (-8 < d6 < 8 and -8 < a0 < 8) or m.rl(a2 + 8): return
    if m.rl(s + 0x3e) != m.rl(s + 0x46) or m.rw(s + 0x1c):
        m.wl(a2 + 0xc, m.rl(_OLD_X)); m.wl(a2 + 0x10, m.rl(_OLD_Y)); return
    if m.rw(_PLAYERS) == 2:
        m.events.append(("unported", "catapult two players"))
    m.wl(a2 + 0xc, m.rl(s + 0xc)); m.wl(a2 + 0x10, m.rl(s + 0x10))
    m.wl(a2 + 0x14, m.rl(a2 + 0x14) - 0x30000); m.wl(a2 + 8, 0xa0000)
    m.wl(a2, (ms.rng_13a98(m, 0x2000) - 0x1000) & 0xffffffff)
    m.wl(a2 + 4, (-0x25000 - ms.rng_13a98(m, 0xa000)) & 0xffffffff)
    m.wb(a2 + 0x36, 2); ms.sound_15884(m)
    m.wb(a2 + 0x1a, 3); m.wb(a2 + 0x58, m.rb(s + 0x1f)); m.wb(a2 + 0x59, 0x0f)
    m.wl(s + 0x36, _CATAPULT_SCRIPT); ms.script_12896(m, s)
    m.events.append(("catapult", hex(s)))


def _gate_outer(m, a2, s, tag, d6, a0):
    """Gate frame (0x0b: d6 -0x0c..0x1b, a0 -8..0xf; 0x0d transposed, 0x2ae1a), below
    GATE_TOP: already inside it before the move (0x0b box for both gates, 0x2ae42): lost
    (+0x57 0x65, 0x25bae(4)); else both axes bounce. The arcade's 0x0d path reads the old
    deltas from frame locals only the 0x0b and 0x0c paths write (stale or stack garbage);
    the model computes them, as Marble Love."""
    fx, fy = (d6, a0) if tag == 0x0b else (a0, d6)
    if not (-0xc <= fx < 0x1c and -8 <= fy < 0x10) or m.rw(_POS_Z) >= _GATE_TOP: return
    pd6 = s16(m.rw(s + 0xc) - m.rw(_OLD_X)); pa0 = s16(m.rw(s + 0x10) - m.rw(_OLD_Y))
    if -0xc <= pd6 < 0x1c and -8 <= pa0 < 0x10 and m.rw(_OLD_Z) < _GATE_TOP:
        m.wb(a2 + 0x57, 0x65); ms.enter_25bae(m, a2, 4)
        m.events.append(("gate lost", hex(s)))
        return
    m.wb(_HIT_X, 1); m.wb(_HIT_Y, 1)


def _gate(m, a2, s, tag, d6, a0):
    """0x2ab88 (0x0b) / 0x2ad20 (0x0d): while the gate sucks (slot state != 0, +0x46 the
    open animation), the mouth pulls the marble towards its centre; in the centre the marble
    is caught: state 0xa (+0x57 0x20 ticks, +0x58 the slot number). Else the frame."""
    if m.rb(s + 0x1a) == 0 or m.rl(s + 0x46) != _GATE_OPEN[tag]: return
    if tag == 0x0b:
        mouth = -0xc <= d6 < 0x1c and -0x20 <= a0 < -8
        centre = 0 <= d6 < 0x10 and -0xf <= a0 < -9
        cx, cy = d6 - 8, a0 + 0xc
    else:
        mouth = -0x20 <= d6 < -8 and -0xc <= a0 < 0x1c
        centre = -0xf <= d6 < -9 and 0 <= a0 < 0x10
        cx, cy = d6 + 0xc, a0 - 8
    if not mouth:
        _gate_outer(m, a2, s, tag, d6, a0); return
    if centre:
        m.wb(a2 + 0x1a, 0xa); m.wb(a2 + 0x57, 0x20); m.wb(a2 + 0x58, m.rb(s + 0x19))
        m.events.append(("gate catch", hex(s)))
        return
    den = _wdenom(cx, cy)
    qx = _divs(s16(cx) << 16, den) if den else 0
    qy = _divs(s16(cy) << 16, den) if den else 0
    m.wl(a2, (m.rl(a2) + (qx << 2)) & 0xffffffff)
    m.wl(a2 + 4, (m.rl(a2 + 4) + (qy << 2)) & 0xffffffff)


def _bounce_0c(m, a2, s, d6, a0):
    """0x2a9a2: box of the slot's MO frame (record +0x3e: bytes x, y, w, h at +4) widened by
    3 px: entering it bounces both axes; already inside: pushed away from the centre at 64 x
    the unit vector, dizzy (state 1, 0x25c74 landing with fall 0, +0x57 0x3c, +0x56 0)."""
    if m.rb(s + 0x1a) == 0: return
    rec = m.rl(m.rl(s + 0x3e))
    x0, y0 = s8(m.rb(rec + 4)), s8(m.rb(rec + 5))
    x1, y1 = s16(x0 + s8(m.rb(rec + 6))), s16(y0 + s8(m.rb(rec + 7)))
    lx, hx = s16(-(x1 + 3)), s16(-(x0 - 3))
    ly, hy = s16(-(y1 + 3)), s16(-(y0 - 3))
    if not (lx <= d6 < hx and ly <= a0 < hy): return
    pd6 = s16(m.rw(s + 0xc) - m.rw(_OLD_X)); pa0 = s16(m.rw(s + 0x10) - m.rw(_OLD_Y))
    if not (lx <= pd6 < hx and ly <= pa0 < hy):
        m.wb(_HIT_Y, 1); m.wb(_HIT_X, 1); return
    dx, dy = s16(d6 + 4), s16(a0 + 4)
    den = _wdenom(dx, dy)
    vx, vy = (_divs(s16(-dx) << 16, den), _divs(s16(-dy) << 16, den)) if den else (0x1000, 0)
    m.wl(a2, (s16(vx) << 6) & 0xffffffff); m.wl(a2 + 4, (s16(vy) << 6) & 0xffffffff)
    if m.rb(a2 + 0x1a) != 1:
        m.wb(a2 + 0x5f, 0); m.wb(a2 + 0x60, 2); m.wl(a2 + 0x5a, _DIZZY_ANIM)
        m.sounds.append(0x39)
    m.wb(a2 + 0x1a, 1)
    ms.land_25c74(m, a2, 0)
    m.wb(a2 + 0x57, 0x3c); m.wb(a2 + 0x56, 0)
    m.events.append(("bounce box", hex(s)))


def contact(m, a2, s, tag, d6, a0):
    """0x29cce tags 0x0a-0x0d (none returns early)."""
    if tag == 0x0a: _catapult_0a(m, a2, s, d6, a0)
    elif tag == 0x0c: _bounce_0c(m, a2, s, d6, a0)
    else: _gate(m, a2, s, tag, d6, a0)


def _pattern_13d38(m, a2):
    """Sucked-in stars: 8 stars (code 0x10b + k) on a line from the marble's screen position
    +0x1e to the gate (slot +0x4e, offset by the gate kind), drawn closer each tick, the first
    4 visible at +0xa4, the next 4 at +0x38; +0x57 counts down. True when it reaches 0."""
    d2 = 0x20 - s8(m.rb(a2 + 0x57)); m.wb(a2 + 0x57, m.rb(a2 + 0x57) - 1)
    s = m.rom_l(_SLOT_LIST + 4 * s8(m.rb(a2 + 0x58)))
    gx, gy = m.rw(s + 0x4e), m.rw(s + 0x50)
    mx, my = m.rw(a2 + 0x1e), m.rw(a2 + 0x20)
    if m.rb(s + 0x1f) == 0x0d:
        gx, gy = gx - 5, gy + 5; ex, ey = gx - 0x1c, gy - 0xe
    else:
        gx, gy = gx + 3, gy + 5; ex, ey = gx + 0x1c, gy - 0xe
    ax, ay = s16(ex - mx), s16(ey - my)       # marble to the gate mouth
    bx, by = s16(gx - ex), s16(gy - ey)       # mouth to the gate
    for k in range(_HALF):
        m.ww(a2 + 0x38 + 6 * k, 0); m.ww(a2 + 0xa4 + 6 * k, 0)
    n, p = 0, _PATTERN
    for k in range(_STARS):
        d1 = s16(d2 - 2 * k)
        if d1 >= 8:
            p += 2; continue
        if d1 < 0:
            x, y = mx, my
        elif d1 >= 4:
            x, y = ((d1 - 4) * bx >> 2) + s16(ex), ((d1 - 4) * by >> 2) + s16(ey)
        else:
            x, y = (d1 * ax >> 2) + s16(mx), (d1 * ay >> 2) + s16(my)
        e = a2 + (0xa4 + 6 * n if n < _HALF else 0x38 + 6 * (n - _HALF)); n += 1
        m.ww(e, _STAR_CODE + k)
        m.ww(e + 2, s8(m.prog[p]) + s16(x)); m.ww(e + 4, s8(m.prog[p + 1]) + s16(y)); p += 2
    m.wb(a2 + 0x1c, 1)
    return m.rb(a2 + 0x57) == 0


def caught_2563e(m, a2):
    """Player state 0xa (refresh-frame-10fce.ts JT[10]): screen position, sucked-in stars;
    when they are done the gate closes (script GATE_CLOSE, sound 0x1ef5a[zone - 0x1e]), sound
    0x3c, death count + 1, state 4 at the same place (+0x57 0x65)."""
    ms.proj_1b9cc(m, a2, True)
    if not _pattern_13d38(m, a2): return
    s = m.rom_l(_SLOT_LIST + 4 * s8(m.rb(a2 + 0x58)))
    m.wl(s + 0x36, _GATE_CLOSE[m.rb(s + 0x1f)])
    m.sounds.append(("1ef5a", m.rb(s + 0x1b)))
    ms.script_12896(m, s)
    m.sounds.append(0x3c)
    m.wl(a2 + 0x5a, 0); m.wb(a2 + 0x1a, 4)
    m.ww(a2 + 0xd2, m.rw(a2 + 0xd2) + 1); m.wb(a2 + 0x57, 0x65)


def enable(m):
    """Aerial objects on: tags 0x0a-0x0d in 0x29cce, player state 0xa."""
    m.aerial = True
