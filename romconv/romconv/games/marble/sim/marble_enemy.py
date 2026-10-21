"""Marble Madness enemy model (Phase 4b research): steelies 0x4009a4 and the object pair
collision 0x1bc88. 1:1 ports named by ROM address (Marble Love helper-182ba.ts,
sub-158f6.ts, helper-1bc88.ts, state-sub-15e24.ts, state-dispatch-1605c.ts,
nearest-neighbor.ts) on the work RAM image of marble_sim.Mem. Facts: MARBLE.md "Enemies".
"""

from types import MappingProxyType

from romconv.core.errors import ConversionError
from romconv.games.marble.sim import marble_sim as ms
from romconv.games.marble.sim.marble_sim import s8, s16, s32

P = ms.P
STEELIES = (0x4009a4, 0x400a20)        # 2 x 0x7c, draw list type 2 (table 0x1effe)
PAIR_LIST = 0x24ac6                    # 0x1bc88 objects: P1, P2, steelie 0, steelie 1
REPEL = 0x24ad6                        # byte per |v| high word
OBJ_PTRS = 0x1eff6                     # P1, P2, steelie 0, steelie 1
_PATROL, _CHASE, _HUNT, _RETURN, _STUN = 0x20, 0x21, 0x22, 0x23, 0x24
_SPEED = MappingProxyType({_CHASE: 0xc0000, _HUNT: 0x80000, _PATROL: 0x70000, _RETURN: 0x70000})


def _octant(dx, dy):
    """max + 3/8 min of |dx| >> 12, |dy| >> 12 (word)."""
    ax = (abs(dx) >> 12) & 0xffff; ay = (abs(dy) >> 12) & 0xffff
    return ((ay >> 3) * 3 + ax if ax > ay else (ax >> 3) * 3 + ay) & 0xffff


def _nearest_15d10(m, a2):
    """+0x6e = node of list +0x72 nearest to the steelie cell (6 byte nodes x, y, 4 links)."""
    rx = (m.sl(a2 + 0xc) >> 19) & 0xffff; ry = (m.sl(a2 + 0x10) >> 19) & 0xffff
    p = m.rl(a2 + 0x72); best = 0x400; bp = m.rl(a2 + 0x6e)
    while m.rb(p) != 0xff and m.rb(p + 1) != 0xff:
        dx = abs(s16(rx - m.sb(p))) << 4 & 0xffff; dy = abs(s16(ry - m.sb(p + 1))) << 4 & 0xffff
        d = ((dx >> 3) * 3 + dy if dx <= dy else (dy >> 3) * 3 + dx) & 0xffff
        if d < best: best = d; bp = p
        p += 6
    m.wl(a2 + 0x6e, bp)


def _enter23_160d4(m, a2):
    m.wb(a2 + 0x1a, _RETURN); _nearest_15d10(m, a2); m.wl(a2 + 0x68, 0x70000)


def _advance_160ae(m, a2, k):
    """+0x6e = node of link k of the current node."""
    m.wl(a2 + 0x6e, (m.rl(a2 + 0x72) + s8(m.rb(m.rl(a2 + 0x6e) + 2 + k)) * 6) & 0xffffffff)


def _best_15c46(m, a2):
    """Link (0-3) whose node is nearest to the target object."""
    t = m.rom_l(OBJ_PTRS + 4 * m.sw(a2 + 0x7a))
    cur = m.rl(a2 + 0x6e); base = m.rl(a2 + 0x72); bi = 0; bd = 0x7fffffff
    for k in range(4):
        n = base + s8(m.rb(cur + 2 + k)) * 6
        d = _octant((m.sb(n) << 19) - m.sl(t + 0xc), (m.sb(n + 1) << 19) - m.sl(t + 0x10))
        if d < bd: bd = d; bi = k
    return bi


def _choose_15e24(m, a2, flag):
    """Pick the mode: chase (same zone, near, angry or zone 7/9), hunt (angry), patrol."""
    old = m.rb(a2 + 0x1a); count = m.rw(0x400396); rem = count & 0xff; cand = 0
    for i in range(count):
        o = P + i * 0xe2
        if m.rb(o + 0x18) != 1 or m.rb(o + 0x1b) != m.rb(a2 + 0x1b) or m.rb(o + 0x36) == 2: continue
        if m.rb(o + 0x1a) not in (0, 1, 5): continue
        rem = (rem - 1) & 0xff; cand = o
    if (s8(rem) & 0xffff) != count:
        if count == 2 and rem == 0:
            raise NotImplementedError("0x15fe6 two players")
        m.ww(a2 + 0x7a, s8(m.rb(cand + 0x19)) & 0xffff)
        d = _octant(m.sl(cand + 0xc) - m.sl(a2 + 0xc), m.sl(cand + 0x10) - m.sl(a2 + 0x10))
        angry = m.rw(a2 + 0x6c) != 0
        if d < 0x280 and (angry or m.rb(a2 + 0x1b) in (7, 9)) and m.rb(a2 + 0x36) == 0:
            m.wb(a2 + 0x1a, _CHASE)
        elif angry: m.wb(a2 + 0x1a, _HUNT)
        elif m.rb(a2 + 0x1a) != _RETURN: m.wb(a2 + 0x1a, _PATROL)
    elif m.rb(a2 + 0x1a) != _RETURN:
        m.wb(a2 + 0x1a, _PATROL)
    new = m.rb(a2 + 0x1a)
    if old != new:
        if old == _CHASE: _enter23_160d4(m, a2)
        else: flag = 1
    if flag:
        m.wl(a2 + 0x68, _SPEED.get(m.rb(a2 + 0x1a), 0))
        st = m.rb(a2 + 0x1a)
        if st == _PATROL: _advance_160ae(m, a2, 0)
        elif st == _HUNT: _advance_160ae(m, a2, _best_15c46(m, a2))


def _node_15db6(m, a2):
    """Node reached (cell == node): return -> patrol; then nearest node or mode choice."""
    n = m.rl(a2 + 0x6e)
    hit = m.sb(n) == m.sl(a2 + 0xc) >> 19 and m.sb(n + 1) == m.sl(a2 + 0x10) >> 19
    if hit and m.rb(a2 + 0x1a) == _RETURN: m.wb(a2 + 0x1a, _PATROL)
    if m.rb(a2 + 0x1a) == _RETURN: _nearest_15d10(m, a2)
    else: _choose_15e24(m, a2, 1 if hit else 0)


def _ai_182ba(m, a2):
    """Steer: velocity toward the target (player in chase, else the current node centre)."""
    _node_15db6(m, a2)
    if m.rb(a2 + 0x36) != 2:
        a0 = m.rom_l(OBJ_PTRS + 4 * m.sw(a2 + 0x7a))
        if m.rb(a2 + 0x1a) == _CHASE and m.rb(a0 + 0x18) == 1 and m.rb(a0 + 0x1b) == m.rb(a2 + 0x1b) \
                and s8(m.rb(a0 + 0x1a)) in (0, 1, 5):
            d2 = s32(m.sl(a0 + 0xc) - m.sl(a2 + 0xc)); d3 = s32(m.sl(a0 + 0x10) - m.sl(a2 + 0x10))
        else:
            a1 = m.rl(a2 + 0x6e)
            d2 = s32((m.sb(a1) << 19) - m.sl(a2 + 0xc) + 0x40000)
            d3 = s32((m.sb(a1 + 1) << 19) - m.sl(a2 + 0x10) + 0x40000)
        d1 = _octant(d2, d3)
        if d1:
            q2 = int(d2 / s16(d1)); q3 = int(d3 / s16(d1))
            d2 = s16(q2); d3 = s16(q3)
        else: d2 = d3 = 0
        if d1 < 0x40: m.wl(a2 + 0x68, 0x10000)
        k = s16((m.sl(a2 + 0x68) >> 8) & 0xffff)
        d2 = (s32(k * d2 >> 4) - m.sl(a2)) >> 5
        d3 = (s32(k * d3 >> 4) - m.sl(a2 + 4)) >> 5
        if m.rb(a2 + 0x1a) == _STUN:
            c = max(0, s16(0x1f - m.sb(a2 + 0x56)))
            d2 = s32((d2 >> 8) * c << 3); d3 = s32((d3 >> 8) * c << 3)
        m.wl(a2, (m.sl(a2) + d2) & 0xffffffff); m.wl(a2 + 4, (m.sl(a2 + 4) + d3) & 0xffffffff)
    if m.rb(a2 + 0x36):
        vz = max(m.sl(a2 + 8) - 0x6000, -0x50000); m.wl(a2 + 8, vz & 0xffffffff)
    ms.speedcap_26196(m, a2)


def remove_15bd0(m, a2, score, clear):
    """clear: slot free + draw list removal 0x18f46; score: 0x285b0(player, 3) for every
    player in play (state-sub-15bd0.ts)."""
    if clear:
        m.wb(a2 + 0x18, 0); m.events.append(("enemy removed", a2))
        if m.drawlist: m.drawlist.remove(m, 2, m.rb(a2 + 0x19))
    if not score: return
    m.events.append(("score", 3))
    for a in ms.players(m):
        if m.rb(a + 0x18) not in (0, 2): ms.score_285b0(m, a, 3)


def _land_np_25c74(m, a2, fall):
    """Non-player landing: stunned, +0x56 += fall; > 0x50 or fall > 0x1f shatters."""
    m.wb(a2 + 0x1a, _STUN)
    d4 = min(s8(m.rb(a2 + 0x57)) + fall, 0x7f); m.wb(a2 + 0x57, d4)
    v = (m.rb(a2 + 0x56) + fall) & 0xff; m.wb(a2 + 0x56, v)
    if s8(v) > 0x50 or fall > 0x1f:
        remove_15bd0(m, a2, 1, 0); m.wb(a2 + 0x18, 2); ms.enter_25bae(m, a2, 2)


SPAWN_TABLE = 0x22706                  # per race: 8 byte entries lo, hi, node list, zone, pad
_SPAWN_HI16 = 0x2276e                   # beginner start entry: hi - 16


def _init_15a12(m, a2, e):
    """Steelie at the first node of entry e's list, patrol, angry timer off, in the draw list."""
    nodes = m.rom_l(e + 2)
    m.wl(a2 + 0x6e, nodes); m.wl(a2 + 0x72, nodes); m.wb(a2 + 0x1b, m.prog[e + 6])
    m.ww(a2 + 0x76, s8(m.prog[e]) & 0xffff); m.ww(a2 + 0x78, s8(m.prog[e + 1]) & 0xffff)
    if nodes == _SPAWN_HI16: m.ww(a2 + 0x78, (m.rw(a2 + 0x78) - 0x10) & 0xffff)
    m.wl(a2 + 0xc, ((s8(m.prog[nodes]) << 19) + 0x40000) & 0xffffffff)
    m.wl(a2 + 0x10, ((s8(m.prog[nodes + 1]) << 19) + 0x40000) & 0xffffffff)
    m.ww(0x400698, 0xffff); m.ww(0x400696, 0xffff)
    ms.posupd_1bab2(m, a2)
    m.wl(a2 + 0x14, ms.height_1cc62(m) & 0xffffffff)
    m.wl(a2 + 4, 0); m.wl(a2, 0)
    m.wb(a2 + 0x18, 1); m.wb(a2 + 0x36, 0); m.wb(a2 + 0x1a, _PATROL)
    m.wl(a2 + 0x22, 0); m.wl(a2 + 0x1e, 0); m.ww(a2 + 0x6c, 0xffff)
    m.wb(a2 + 0x67, 0); m.wb(a2 + 0x56, 0); m.wb(a2 + 0x58, 0)
    for i in range(5): m.ww(a2 + 0x38 + 6 * i, 0)
    ms.proj_1b9cc(m, a2)
    if m.drawlist: m.drawlist.insert(m, 2, m.rb(a2 + 0x19))


def spawn_15a12(m, old, new):
    """Camera band step (0x144e4): spawn a steelie for every entry of 0x22706[race] whose lo or
    hi band is entered from outside [lo, hi] and whose node list no live steelie has (free
    slot = highest free index, 0x1599a); then remove steelies whose band range is left."""
    old = s8(old); new = s8(new)
    e = m.rom_l(SPAWN_TABLE + 4 * m.rw(0x400394))
    while m.prog[e] != 0xff:
        free = [a for a in STEELIES if not m.rb(a + 0x18)]
        if not free: break
        lo, hi = s8(m.prog[e]), s8(m.prog[e + 1])
        taken = any(m.rb(a + 0x18) and m.rl(a + 0x72) == m.rom_l(e + 2) for a in STEELIES)
        if not taken and new in (lo, hi) and not lo <= old <= hi: _init_15a12(m, free[-1], e)
        e += 8
    for a in STEELIES:
        if not m.rb(a + 0x18): continue
        lo, hi = m.sw(a + 0x76), m.sw(a + 0x78)
        if (old == lo and new < lo) or (old == hi and new > hi): remove_15bd0(m, a, 0, 1)


def _pair_1bc88(m, a2):
    """Object pair collision: a2 against P1, P2 and (beginner, aerial, ultimate) the steelies.
    Overlap 7 px x/y, 14 px z, octant < 0x70: a2 back to its old position, velocities swapped,
    both pushed apart; an enemy gets stunned (0x24, angry 150 ticks); a2 falling onto a3 breaks
    a3 (player state 2, steelie shatters)."""
    race = m.sw(0x400394); n = 4 if race in (1, 3, 5) else 2
    sx, sy, sz = m.sw(0x400690), m.sw(0x400692), m.sw(0x400694)
    hit = 0
    for i in range(n):
        a3 = m.rom_l(PAIR_LIST + 4 * i)
        if a3 == a2 or m.rb(a3 + 0x18) != 1: continue
        dx = s16(sx - m.rw(a3 + 0xc)); dy = s16(sy - m.rw(a3 + 0x10))
        if not (-7 <= dx <= 7 and -7 <= dy <= 7): continue
        dz = s16(sz - m.rw(a3 + 0x14))
        if not (-14 <= dz <= 14): continue
        if m.rb(a3 + 0x1a) in (4, 2, 0xb) or m.rb(a2 + 0x58) == 0xa: continue
        if _octant(abs(dx) << 16, abs(dy) << 16) >= 0x70: continue
        hit = 1
        pa, pb = ms.is_player(a2), ms.is_player(a3)
        m.wl(a2 + 0xc, m.rl(0x400684)); m.wl(a2 + 0x10, m.rl(0x400688)); m.wl(a2 + 0x14, m.rl(0x40068c))
        for o in (0, 4):
            t = m.rl(a2 + o); m.wl(a2 + o, m.rl(a3 + o)); m.wl(a3 + o, t)

        def push(obj, o, by):
            v = s8(m.prog[REPEL + (abs(m.sw(obj + o)) & 0xff)]) << 12
            if pa != pb: v <<= 2
            return -v if m.sl(a2 + by) > m.sl(a3 + by) else v
        ax, ay = push(a2, 0, 0xc), push(a2, 4, 0x10)
        m.wl(a2, (m.sl(a2) - ax) & 0xffffffff); m.wl(a2 + 4, (m.sl(a2 + 4) - ay) & 0xffffffff)
        bx, by_ = push(a3, 0, 0xc), push(a3, 4, 0x10)
        m.wl(a3, (m.sl(a3) + bx) & 0xffffffff); m.wl(a3 + 4, (m.sl(a3 + 4) + by_) & 0xffffffff)
        for obj, pl in ((a2, pa), (a3, pb)):
            if pl: continue
            if m.rw(obj + 0x6c) == 0xffff: m.ww(obj + 0x6c, 0x96)
            m.wb(obj + 0x1a, _STUN); m.wb(obj + 0x56, m.rb(obj + 0x56) + 0x14)
            _enter23_160d4(m, obj)
        m.sounds.append(0x37 if pa and pb else 0x32)
        m.events.append(("pair", hex(a2), hex(a3)))
        if dz < 0 or m.rb(a2 + 0x36) == 0: continue
        if not pb: remove_15bd0(m, a3, 1, 0); m.wb(a3 + 0x18, 2)
        ms.enter_25bae(m, a3, 2)
    return hit


def _hook(m, a2):
    """0x1239c: hit flags of 0x29cce -> 0x1bab2, then 0x1bc88."""
    if m.rb(0x400666) or m.rb(0x400668): ms.posupd_1bab2(m, a2)
    return _pair_1bc88(m, a2)


def update_158f6(m, a2):
    """0x158cc per steelie: anger timer, stun timer, then the shatter animation (+0x18 == 2)
    or 0x253bc + AI 0x182ba + the player physics 0x121b8."""
    if not m.rb(a2 + 0x18): return
    t = m.sw(a2 + 0x6c)
    if t > 0:
        m.ww(a2 + 0x6c, t - 1)
        if t == 1 and m.rb(a2 + 0x1a) in (_CHASE, _HUNT): _enter23_160d4(m, a2)
    if m.rb(a2 + 0x1a) == _STUN:
        if m.rb(a2 + 0x56): m.wb(a2 + 0x56, m.rb(a2 + 0x56) - 1)
        if not m.rb(a2 + 0x56): _enter23_160d4(m, a2)
    elif m.rb(a2 + 0x18) == 2:                         # shattering (sub-158f6.ts block 3)
        m.events.append(("shatter anim", a2))
        ms.anim_25fc2(m, a2)
        ms.proj_1b9cc(m, a2, refetch=(a2 & 0xff) != 0)  # flag = low byte of the pushed a2
        ms.enter_1281c(m, a2)
        return
    ms.ground_253bc(m, a2)
    _ai_182ba(m, a2)
    ms.physics_121b8(m, a2)


def enable(m):
    """Steelies on: band spawns, 0x158cc update in main_tick, 0x1bc88, non-player landing."""
    m.enemies = True; m.pair = _hook; m.land = _land_np_25c74


def setup(m):
    """main_tick model with deaths, slots, sprites, draw list, steelies, slinkies, acid, birds,
    minis, pistons, aerial objects (the arcade runs all; fixture RAM blocks then hold the slinky and acid
    records too)."""
    from romconv.games.marble.sim import marble_mo, marble_slinky, marble_acid, marble_bird, marble_mini, marble_piston
    from romconv.games.marble.sim import marble_aerial
    m.deaths = m.objects = m.sprites = True; m.drawlist = marble_mo.DrawList(); enable(m)
    marble_slinky.enable(m); marble_acid.enable(m); marble_bird.enable(m); marble_mini.enable(m)
    marble_piston.enable(m); marble_aerial.enable(m)
    return m


def drawn(m):
    """Draw list in link order as (type, index)."""
    out = []
    for k in range(31):
        e = m.rb(0x4003bc + k)
        if e == 0xff: break
        out.append((m.rb(0x4001dc + 14 * e), m.rb(0x4001dd + 14 * e)))
    return out


def steelie_mos(m, i):
    """MOs of steelie i if it is in the draw list."""
    from romconv.games.marble.sim import marble_mo
    return marble_mo.other_mos(m, 2, i) if (2, i) in drawn(m) else []


_FRAME = 0x21f36                        # steelie frame (set 5)


def steelie_obj(m, i):
    """Steelie fixture check values: +0x18, +0x1a, +0x1c, x, y, z, sx, sy, +0x5a, the drawn
    frame header (FRAME, rl(+0x5a) while shattering, 0 if not drawn), the overlay header
    (rl(+0x62) when +0x67), anger +0x6c, node +0x6e, masks +0x38 (30 bytes), its MOs."""
    a = STEELIES[i]; o = a - 0x400000
    shown = m.rb(a + 0x18) and m.rb(a + 0x1c) and (2, i) in drawn(m)
    hdr = (m.rl(m.rl(a + 0x5a)) if m.rb(a + 0x18) == 2 else _FRAME) if shown else 0
    shard = m.rl(m.rl(a + 0x62)) if shown and m.rb(a + 0x67) else 0
    return (m.rb(a + 0x18), m.rb(a + 0x1a), m.rb(a + 0x1c), m.sl(a + 0xc), m.sl(a + 0x10),
            m.sl(a + 0x14), m.sw(a + 0x1e), m.sw(a + 0x20), m.rl(a + 0x5a), hdr, shard,
            m.rw(a + 0x6c), m.rl(a + 0x6e), bytes(m.ram[o + 0x38:o + 0x56]),
            tuple(steelie_mos(m, i)))


def tick_state(m):
    """Steelie fixture values of one tick: player (marble_sim.death_state), both steelies,
    the draw list in link order (type, index)."""
    return (ms.death_state(m), steelie_obj(m, 0), steelie_obj(m, 1), tuple(drawn(m)))


def load_sfixture(path):
    """Segments of source/test/test_marble_steelies/marble_steelies_trace.h (make_fixture.py
    stwrite): [{"race", "frame", "ram", "ticks": [(c6, c7, camera, frames, tick_state)]}]."""
    import re
    text = open(path).read()
    num = lambda s: [int(v, 0) for v in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", s)]
    arrays = {name: body for name, body in
              re.findall(r"static const \w+ (\w+)\[\] = \{(.*?)\n?\};", text, re.S)}
    segs = []
    for race, frame, nram, ramname, nticks, tickname, nmos, moname, nlist, listname in re.findall(
            r"\{ *(\d+), *(\d+), *(\d+), *(\w+), *(\d+), *(\w+), *(\d+), *(\w+), *(\d+), *(\w+) *\}",
            arrays["marble_steelies_segments"]):
        ram = [(int(addr, 0), bytes(num(arrays[data]))) for addr, _, data in
               re.findall(r"\{ *(0x[0-9a-f]+), *(\d+), *(\w+) *\}", arrays[ramname])]
        mw = num(arrays[moname]); lw = num(arrays[listname])
        mos = [tuple(mw[k:k + 3]) for k in range(0, len(mw), 3)]
        pairs = [tuple(lw[k:k + 2]) for k in range(0, len(lw), 2)]
        v = num(arrays[tickname]); w = 4 + 43 + 2 * 45 + 2
        assert len(v) == w * int(nticks)
        ticks = []
        for k in range(0, len(v), w):
            t = v[k:k + w]
            c6, c7, cam, frames = t[:4]
            p = t[4:47]
            player = tuple(p[:10]) + (bytes(p[10:40]), p[40], tuple(mos[p[42]:p[42] + p[41]]))
            objs = []
            for j in range(2):
                q = t[47 + 45 * j:92 + 45 * j]
                objs.append(tuple(q[:13]) + (bytes(q[13:43]), tuple(mos[q[44]:q[44] + q[43]])))
            n, first = t[137:139]
            ticks.append((c6, c7, cam, frames, (player, objs[0], objs[1],
                                                tuple(pairs[first:first + n]))))
        segs.append({"race": int(race), "frame": int(frame), "ram": ram, "ticks": ticks})
    return segs


def sweep(m, race):
    """Race sweep with steelies on a cleared RAM (setup(m) done): the remake race start
    (marble_sim.begin_race), then main_tick + 0x26f3e sort per camera step of
    marble_mo.race_sweep (top to bottom, back, again; silly reversed), player idle. Checks
    every steelie spawn/removal against the spawn table bands. Returns (per tick (live
    steelies, draw list), spawns, removals)."""
    from romconv.games.marble.sim import marble_mo
    ms.begin_race(m, race)
    top, rng = m.rom_w(m.t.desc + 0x10), m.rom_w(m.t.desc + 0x12)
    cams = list(range(top, top + rng + 1, marble_mo.SWEEP_STEP))
    if race == 4: cams.reverse()
    cams = cams + cams[::-1] + cams
    out, spawns, removals = [], 0, 0
    for k, cam in enumerate(cams):
        before = [(m.rb(a + 0x18), m.rl(a + 0x72)) for a in STEELIES]
        old = m.sw(0x40097e)
        ms.main_tick(m, 0, 0, cam & 0xffffffff, k + 1)
        marble_mo.boxes_and_sort(m)
        lst = drawn(m)
        for i, a in enumerate(STEELIES):
            live = m.rb(a + 0x18)
            if not before[i][0] and live:
                spawns += 1
                band = s16(cam - top) >> 4
                if band not in (m.sw(a + 0x76), m.sw(a + 0x78) + (16 if m.rl(a + 0x72) == _SPAWN_HI16 else 0)):
                    raise ConversionError("race %d cam %d: steelie %d spawned at band %d" % (race, cam, i, band))
            if before[i][0] and not live: removals += 1
            if bool(live) != ((2, i) in lst):
                raise ConversionError("race %d cam %d: steelie %d live %d, draw list %s" % (race, cam, i, live, lst))
        out.append((tuple(m.rb(a + 0x18) for a in STEELIES), tuple(lst)))
    return out, spawns, removals
