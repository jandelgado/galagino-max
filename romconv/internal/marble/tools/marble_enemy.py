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
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import marble_sim as ms
from marble_sim import s8, s16, s32, rom_l, rom_w, PROG

P = ms.P
STEELIES = (0x4009a4, 0x400a20)        # 2 x 0x7c, draw list type 2 (table 0x1effe)
STEELIE_SIZE = 0x7c
PAIR_LIST = 0x24ac6                    # 0x1bc88 objects: P1, P2, steelie 0, steelie 1
REPEL = 0x24ad6                        # byte per |v| high word
OBJ_PTRS = 0x1eff6                     # P1, P2, steelie 0, steelie 1
PATROL, CHASE, HUNT, RETURN, STUN = 0x20, 0x21, 0x22, 0x23, 0x24
SPEED = {CHASE: 0xc0000, HUNT: 0x80000, PATROL: 0x70000, RETURN: 0x70000}


def octant(dx, dy):
    """max + 3/8 min of |dx| >> 12, |dy| >> 12 (word)."""
    ax = (abs(dx) >> 12) & 0xffff; ay = (abs(dy) >> 12) & 0xffff
    return ((ay >> 3) * 3 + ax if ax > ay else (ax >> 3) * 3 + ay) & 0xffff


def nearest_15d10(m, a2):
    """+0x6e = node of list +0x72 nearest to the steelie cell (6 byte nodes x, y, 4 links)."""
    rx = (m.sl(a2 + 0xc) >> 19) & 0xffff; ry = (m.sl(a2 + 0x10) >> 19) & 0xffff
    p = m.rl(a2 + 0x72); best = 0x400; bp = m.rl(a2 + 0x6e)
    while m.rb(p) != 0xff and m.rb(p + 1) != 0xff:
        dx = abs(s16(rx - m.sb(p))) << 4 & 0xffff; dy = abs(s16(ry - m.sb(p + 1))) << 4 & 0xffff
        d = ((dx >> 3) * 3 + dy if dx <= dy else (dy >> 3) * 3 + dx) & 0xffff
        if d < best: best = d; bp = p
        p += 6
    m.wl(a2 + 0x6e, bp)


def enter23_160d4(m, a2):
    m.wb(a2 + 0x1a, RETURN); nearest_15d10(m, a2); m.wl(a2 + 0x68, 0x70000)


def advance_160ae(m, a2, k):
    """+0x6e = node of link k of the current node."""
    m.wl(a2 + 0x6e, (m.rl(a2 + 0x72) + s8(m.rb(m.rl(a2 + 0x6e) + 2 + k)) * 6) & 0xffffffff)


def best_15c46(m, a2):
    """Link (0-3) whose node is nearest to the target object."""
    t = rom_l(OBJ_PTRS + 4 * m.sw(a2 + 0x7a))
    cur = m.rl(a2 + 0x6e); base = m.rl(a2 + 0x72); bi = 0; bd = 0x7fffffff
    for k in range(4):
        n = base + s8(m.rb(cur + 2 + k)) * 6
        d = octant((m.sb(n) << 19) - m.sl(t + 0xc), (m.sb(n + 1) << 19) - m.sl(t + 0x10))
        if d < bd: bd = d; bi = k
    return bi


def choose_15e24(m, a2, flag):
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
        d = octant(m.sl(cand + 0xc) - m.sl(a2 + 0xc), m.sl(cand + 0x10) - m.sl(a2 + 0x10))
        angry = m.rw(a2 + 0x6c) != 0
        if d < 0x280 and (angry or m.rb(a2 + 0x1b) in (7, 9)) and m.rb(a2 + 0x36) == 0:
            m.wb(a2 + 0x1a, CHASE)
        elif angry: m.wb(a2 + 0x1a, HUNT)
        elif m.rb(a2 + 0x1a) != RETURN: m.wb(a2 + 0x1a, PATROL)
    elif m.rb(a2 + 0x1a) != RETURN:
        m.wb(a2 + 0x1a, PATROL)
    new = m.rb(a2 + 0x1a)
    if old != new:
        if old == CHASE: enter23_160d4(m, a2)
        else: flag = 1
    if flag:
        m.wl(a2 + 0x68, SPEED.get(m.rb(a2 + 0x1a), 0))
        st = m.rb(a2 + 0x1a)
        if st == PATROL: advance_160ae(m, a2, 0)
        elif st == HUNT: advance_160ae(m, a2, best_15c46(m, a2))


def node_15db6(m, a2):
    """Node reached (cell == node): return -> patrol; then nearest node or mode choice."""
    n = m.rl(a2 + 0x6e)
    hit = m.sb(n) == m.sl(a2 + 0xc) >> 19 and m.sb(n + 1) == m.sl(a2 + 0x10) >> 19
    if hit and m.rb(a2 + 0x1a) == RETURN: m.wb(a2 + 0x1a, PATROL)
    if m.rb(a2 + 0x1a) == RETURN: nearest_15d10(m, a2)
    else: choose_15e24(m, a2, 1 if hit else 0)


def ai_182ba(m, a2):
    """Steer: velocity toward the target (player in chase, else the current node centre)."""
    node_15db6(m, a2)
    if m.rb(a2 + 0x36) != 2:
        a0 = rom_l(OBJ_PTRS + 4 * m.sw(a2 + 0x7a))
        if m.rb(a2 + 0x1a) == CHASE and m.rb(a0 + 0x18) == 1 and m.rb(a0 + 0x1b) == m.rb(a2 + 0x1b) \
                and s8(m.rb(a0 + 0x1a)) in (0, 1, 5):
            d2 = s32(m.sl(a0 + 0xc) - m.sl(a2 + 0xc)); d3 = s32(m.sl(a0 + 0x10) - m.sl(a2 + 0x10))
        else:
            a1 = m.rl(a2 + 0x6e)
            d2 = s32((m.sb(a1) << 19) - m.sl(a2 + 0xc) + 0x40000)
            d3 = s32((m.sb(a1 + 1) << 19) - m.sl(a2 + 0x10) + 0x40000)
        d1 = octant(d2, d3)
        if d1:
            q2 = int(d2 / s16(d1)); q3 = int(d3 / s16(d1))
            d2 = s16(q2); d3 = s16(q3)
        else: d2 = d3 = 0
        if d1 < 0x40: m.wl(a2 + 0x68, 0x10000)
        k = s16((m.sl(a2 + 0x68) >> 8) & 0xffff)
        d2 = (s32(k * d2 >> 4) - m.sl(a2)) >> 5
        d3 = (s32(k * d3 >> 4) - m.sl(a2 + 4)) >> 5
        if m.rb(a2 + 0x1a) == STUN:
            c = max(0, s16(0x1f - m.sb(a2 + 0x56)))
            d2 = s32((d2 >> 8) * c << 3); d3 = s32((d3 >> 8) * c << 3)
        m.wl(a2, (m.sl(a2) + d2) & 0xffffffff); m.wl(a2 + 4, (m.sl(a2 + 4) + d3) & 0xffffffff)
    if m.rb(a2 + 0x36):
        vz = max(m.sl(a2 + 8) - 0x6000, -0x50000); m.wl(a2 + 8, vz & 0xffffffff)
    ms.speedcap_26196(m, a2)


def remove_15bd0(m, a2, score, clear):
    """clear: slot free + draw list removal 0x18f46; score: 0x285b0(player, 3) (not modelled)."""
    if clear: m.wb(a2 + 0x18, 0); m.events.append(("enemy removed", a2))
    if score: m.events.append(("score", 3))


def land_np_25c74(m, a2, fall):
    """Non-player landing: stunned, +0x56 += fall; > 0x50 or fall > 0x1f shatters."""
    m.wb(a2 + 0x1a, STUN)
    d4 = min(s8(m.rb(a2 + 0x57)) + fall, 0x7f); m.wb(a2 + 0x57, d4)
    v = (m.rb(a2 + 0x56) + fall) & 0xff; m.wb(a2 + 0x56, v)
    if s8(v) > 0x50 or fall > 0x1f:
        remove_15bd0(m, a2, 1, 0); m.wb(a2 + 0x18, 2); broken_25bae(m, a2)


def broken_25bae(m, a2):
    """0x25bae(obj, 2): shatter animation list 0x20fde (+0x5a), 2 ticks per frame."""
    m.wl(a2 + 4, 0); m.wl(a2, 0)
    if m.rb(a2 + 0x1a) == 6: m.wb(a2 + 0x18, 3)
    m.wb(a2 + 0x5f, 0); m.wb(a2 + 0x60, 2); m.wl(a2 + 0x5a, 0x20fde); m.wb(a2 + 0x56, 2)
    m.wb(a2 + 0x1a, 2); m.events.append(("broken", a2)); m.sounds.append("25bae case 2")


def pair_1bc88(m, a2):
    """Object pair collision: a2 against P1, P2 and (beginner, aerial, ultimate) the steelies.
    Overlap 7 px x/y, 14 px z, octant < 0x70: a2 back to its old position, velocities swapped,
    both pushed apart; an enemy gets stunned (0x24, angry 150 ticks); a2 falling onto a3 breaks
    a3 (player state 2, steelie shatters)."""
    race = m.sw(0x400394); n = 4 if race in (1, 3, 5) else 2
    sx, sy, sz = m.sw(0x400690), m.sw(0x400692), m.sw(0x400694)
    hit = 0
    for i in range(n):
        a3 = rom_l(PAIR_LIST + 4 * i)
        if a3 == a2 or m.rb(a3 + 0x18) != 1: continue
        dx = s16(sx - m.rw(a3 + 0xc)); dy = s16(sy - m.rw(a3 + 0x10))
        if not (-7 <= dx <= 7 and -7 <= dy <= 7): continue
        dz = s16(sz - m.rw(a3 + 0x14))
        if not (-14 <= dz <= 14): continue
        if m.rb(a3 + 0x1a) in (4, 2, 0xb) or m.rb(a2 + 0x58) == 0xa: continue
        if octant(abs(dx) << 16, abs(dy) << 16) >= 0x70: continue
        hit = 1
        pa, pb = ms.is_player(a2), ms.is_player(a3)
        m.wl(a2 + 0xc, m.rl(0x400684)); m.wl(a2 + 0x10, m.rl(0x400688)); m.wl(a2 + 0x14, m.rl(0x40068c))
        for o in (0, 4):
            t = m.rl(a2 + o); m.wl(a2 + o, m.rl(a3 + o)); m.wl(a3 + o, t)

        def push(obj, o, by):
            v = s8(PROG[REPEL + (abs(m.sw(obj + o)) & 0xff)]) << 12
            if pa != pb: v <<= 2
            return -v if m.sl(a2 + by) > m.sl(a3 + by) else v
        ax, ay = push(a2, 0, 0xc), push(a2, 4, 0x10)
        m.wl(a2, (m.sl(a2) - ax) & 0xffffffff); m.wl(a2 + 4, (m.sl(a2 + 4) - ay) & 0xffffffff)
        bx, by_ = push(a3, 0, 0xc), push(a3, 4, 0x10)
        m.wl(a3, (m.sl(a3) + bx) & 0xffffffff); m.wl(a3 + 4, (m.sl(a3 + 4) + by_) & 0xffffffff)
        for obj, pl in ((a2, pa), (a3, pb)):
            if pl: continue
            if m.rw(obj + 0x6c) == 0xffff: m.ww(obj + 0x6c, 0x96)
            m.wb(obj + 0x1a, STUN); m.wb(obj + 0x56, m.rb(obj + 0x56) + 0x14)
            enter23_160d4(m, obj)
        m.sounds.append(0x37 if pa and pb else 0x32)
        m.events.append(("pair", hex(a2), hex(a3)))
        if dz < 0 or m.rb(a2 + 0x36) == 0: continue
        if not pb: remove_15bd0(m, a3, 1, 0); m.wb(a3 + 0x18, 2)
        broken_25bae(m, a3)
    return hit


def hook(m, a2):
    """0x1239c: hit flags of 0x29cce -> 0x1bab2, then 0x1bc88."""
    if m.rb(0x400666) or m.rb(0x400668): ms.posupd_1bab2(m, a2)
    return pair_1bc88(m, a2)


def update_158f6(m, a2):
    """0x158cc per steelie: anger timer, stun timer, then the shatter animation (+0x18 == 2)
    or 0x253bc + AI 0x182ba + the player physics 0x121b8."""
    if not m.rb(a2 + 0x18): return
    t = m.sw(a2 + 0x6c)
    if t > 0:
        m.ww(a2 + 0x6c, t - 1)
        if t == 1 and m.rb(a2 + 0x1a) in (CHASE, HUNT): enter23_160d4(m, a2)
    if m.rb(a2 + 0x1a) == STUN:
        if m.rb(a2 + 0x56): m.wb(a2 + 0x56, m.rb(a2 + 0x56) - 1)
        if not m.rb(a2 + 0x56): enter23_160d4(m, a2)
    elif m.rb(a2 + 0x18) == 2:
        m.events.append(("shatter anim", a2)); return     # 0x25fc2, 0x1b9cc, 0x1281c
    ms.ground_253bc(m, a2)
    ai_182ba(m, a2)
    ms.physics_121b8(m, a2)
    for e in list(m.events):
        if e in ("embedded", "fell"):                   # non-player: 0x15bd0(a2, 1, 1)
            m.events.append(("steelie lost", a2))


def mem(t, r):
    m = ms.Mem(t, r); m.sprites = True; m.pair = hook; m.land = land_np_25c74
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


def main(argv):
    cmd = argv[1]
    if cmd == "steelie": return 0 if cmd_steelie(ms.load_trace(argv[2]), int(argv[3])) else 1
    if cmd == "spawn": cmd_spawn(ms.load_trace(argv[2]), int(argv[3])); return 0
    print(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
