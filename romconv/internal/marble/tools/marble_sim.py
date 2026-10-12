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
`run` copies slots, dynamic codes and enemy scratch from the trace each tick; `slots` only
the camera and enemy scratch. Divergences at deaths and enemy contacts end a segment and
are reported, any other divergence fails. Enemies are not ported.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import marble_data as md


def s8(v): v &= 0xff; return v - 0x100 if v & 0x80 else v
def s16(v): v &= 0xffff; return v - 0x10000 if v & 0x8000 else v
def s32(v): v &= 0xffffffff; return v - 0x100000000 if v & 0x80000000 else v


ROM = md.load()
PROG = bytearray(0x30000)
for _base, _d in ROM.prog.items():
    PROG[_base:_base + len(_d)] = _d

BANK_RASTER = 0x24994   # byte per race: slapstic bank at the start of each raster chunk
BANK_TERRAIN = 0x239a0  # word per race: slapstic bank of terrain data (0x400662)
P = 0x400018            # player 1 object (player 2: 0x4000fa), stride 0xe2


def rom_w(a): return PROG[a] << 8 | PROG[a + 1]
def rom_l(a): return rom_w(a) << 16 | rom_w(a + 2)


# ---------------------------------------------------------------- terrain (romconv side)

class Rd:
    """ROM reader with the slapstic bank the 68010 would see."""
    def __init__(self, race):
        self.race = race
        self.bank = 0
        self.desc = rom_l(0x2be00 + 4 * race)
    def b(self, a):
        if a < 0x30000: return PROG[a]
        if 0x80000 <= a < 0x88000: return ROM.slap[self.bank * 0x2000 + (a & 0x1fff)]
        raise ValueError(hex(a))
    def w(self, a): return self.b(a) << 8 | self.b(a + 1)
    def l(self, a): return self.w(a) << 16 | self.w(a + 2)


def slapstic_2bc5c(rd, a4):
    """Bank after the slapstic 103 accesses of 0x2bc5c(a4) (+0x2ff40 alt sequence, 68010
    prefetch of 0x2ff5a acts as alt1). States: A active, B bit-load, C alt-commit."""
    st = "A"; loaded = 0
    if a4 & 1: st = "B"                     # 0x86984: bit1
    if a4 & 2: st = "A"                     # 0x80000: reset
    if a4 & 0x10 and st == "A":             # 0x2ff5a, 0x87a28, 0x87a48 + 2 * i: alt1-3
        st = "C"; loaded = (a4 >> 2) & 3
    if a4 & 0x80: st = "A"                  # 0x80000: reset
    if st == "A": rd.bank = (a4 >> 5) & 3   # 0x80080 + (a4 & 0x60): bank select
    elif st == "C": rd.bank = loaded        # alt4 commit
    # st B: bit2 match only, bank unchanged

def raster_1ad54(rd, S, ent, d5, d4, limit, flag):
    """Rasterise one terrain rectangle into the vertex scratch S (list of [h0,h1,h2,h3]).
    Every entry, drawn or not, switches the slapstic bank via 0x2bc5c."""
    b0 = s8(rd.b(ent)); b1 = rd.b(ent + 1); b2 = s8(rd.b(ent + 2)); b3 = rd.b(ent + 3)
    a4 = 0 if b0 & 1 else 1
    if (b3 + b2) & 4: a4 |= 2
    b5 = rd.b(ent + 5); b6 = rd.b(ent + 6)
    if b5 & 2: a4 |= (b5 & 0xc) + 0x10
    a4 |= b6 & 0xe0
    if flag: raster_draw(rd, S, ent, d5, d4, limit)
    slapstic_2bc5c(rd, a4)

def raster_draw(rd, S, ent, d5, d4, limit):
    """0x1aed4/0x1aff6: a3 runs over cell x (bytes 0, 2), d6 over cell y (bytes 1, 3)."""
    b0 = s8(rd.b(ent)); b1 = rd.b(ent + 1); b2 = s8(rd.b(ent + 2)); b3 = rd.b(ent + 3)
    r0 = b0; r1 = s16(b2 + b0 - 1); c0 = b1; c1 = s16(b3 + b1 - 1)
    base = rd.w(ent + 4)
    d1 = rd.b(ent + 6) & 0x1f; d3 = rd.b(ent + 7)
    pat0 = rd.l(rd.l(rd.desc + 0x20) + 4 * d1)
    pat = pat0
    sub = d3 & 8; dirn = d3 & 7
    dx = s16(rd.w(0x1ecea + 4 * dirn)); dy = s16(rd.w(0x1ecea + 4 * dirn + 2))
    if dx < 0: a3s, a3e = r1, s16(r0 - 1)
    else: a3s, a3e = r0, s16(r1 + 1)
    if dy < 0: d6s, d6e = c1, s16(c0 - 1)
    else: d6s, d6e = c0, s16(c1 + 1)
    def cell(a3, d6):
        nonlocal pat
        a1 = s16(d6 + a3 - d5)
        if a1 >= 0 and a1 < limit:
            a0 = s16((a1 >> 1) + s16(d4 - a3))
            if a0 >= 0 and a0 < 0x16 - (a1 & 1):
                v = rd.b(pat)
                val = (base - s8(v)) & 0xffff if sub else (s8(v) + base) & 0xffff
                h = S[a1 * 0x16 + a0]
                if a3 == r0:
                    if d6 == c0: h[2] = val
                    elif d6 != c1: h[2] = val; h[3] = val
                    else: h[3] = val
                elif a3 != r1:
                    if d6 == c0: h[1] = val; h[2] = val
                    elif d6 != c1: h[0] = h[1] = h[2] = h[3] = val
                    else: h[0] = val; h[3] = val
                else:
                    if d6 == c0: h[1] = val
                    elif d6 != c1: h[0] = val; h[1] = val
                    else: h[0] = val
        pat += 1
        if rd.b(pat) == 0x80: pat = pat0
    if dirn < 4:
        d6 = d6s
        while d6 != d6e:
            a3 = a3s
            while a3 != a3e:
                cell(a3, d6); a3 = s16(a3 + dx)
            d6 = s16(d6 + dy)
    else:
        a3 = a3s
        while a3 != a3e:
            d6 = d6s
            while d6 != d6e:
                cell(a3, d6); d6 = s16(d6 + dy)
            a3 = s16(a3 + dx)

def bsearch_1abd4(rd, base, end, target):
    a2 = base; step = 0x400
    while True:
        w = rd.w(a2)
        if target == w: return (a2 - base) >> 1
        a2 = a2 - step if target < w else a2 + step
        if end < a2: a2 = end
        elif a2 < base: a2 = base
        step >>= 1
        if step == 0 and rd.w(a2) != target:
            raise ValueError("code %04x not in table" % target)

def encode_1aa38(rd, verts, bit, roww, base, end):
    """4 heights per vertex -> terrain code -> 10 bit index into the code table (desc +0x26)."""
    recs = rd.l(rd.desc)
    out = []
    for k in range(0x16):
        if k == 0x15 and bit:
            out.append(0); break
        a3, a4, a5, a6 = (s16(v) for v in verts[k])
        lower = s16(roww - 0x40); upper = s16(roww + 0x40)
        target = None; transform = False
        if a3 != 0 and a3 < 0x1000: target = a3  # code from a vertex override (signed test)
        if target is None:
            for v in (a3, a4, a5, a6):
                if v != 0 and (lower > v or upper <= v): transform = True; break
        if target is None and not transform:
            if a3 == a4 == a5 == a6:
                target = 0 if a3 == 0 else s16(a3 - roww + 0xf040)
            else:
                d4 = d5 = a3
                if d4 != a4: d5 = a4
                elif d4 != a5: d5 = a5
                elif d4 != a6: d5 = a6
                if (a5 != d4 and a5 != d5) or (a6 != d4 and a6 != d5):
                    transform = True
                else:
                    if d4 <= d5:
                        d0 = d5; d3 = 0x1000 if d4 == 0 else s16(d5 - d4)
                    else:
                        d0 = d4; d3 = 0x1000 if d5 == 0 else s16(d4 - d5)
                    ok = True
                    if d3 == 0x1000: d3 = 0x1f
                    elif d3 > 0x60: transform = True; ok = False
                    else:
                        d3 = s16(rd.w(0x1ec28 + (d3 & 0xffff) * 2))
                        if d3 < 0: transform = True; ok = False
                    if ok:
                        d1 = s16(d0 - roww + 0x40)
                        d1 = (d1 + (d3 << 7)) & 0xffff
                        if d0 == a4: d1 = (d1 + 0x1000) & 0xffff
                        if d0 == a5: d1 = (d1 + 0x2000) & 0xffff
                        if d0 == a6: d1 = (d1 + 0x4000) & 0xffff
                        if d0 == a3: d1 = (d1 + 0x8000) & 0xffff
                        target = s16(d1)
        if target is None:
            delta = s16(0x80 - roww)
            a4 = s16(a4 + delta) if a4 else 0
            a5 = s16(a5 + delta) if a5 else 0
            a6 = s16(a6 + delta) if a6 else 0
            a3 = s16(a3 + delta) if a3 else 0
            packed = (((a4 << 8) + a5) & 0xffff) << 16 | (((a6 << 8) + a3) & 0xffff)
            a1 = recs
            while True:
                probe = rd.l(a1); a1 += 4
                if probe == 0xffffffff: break
                if rd.l(a1) == packed: break
            target = s16(a1 - recs)
        out.append(bsearch_1abd4(rd, base, end, target & 0xffff))
    while len(out) < 0x16: out.append(0)
    return out


class Terrain:
    """Per race terrain as built by 0x1a444 at level init (0x16f6c).

    diag[d][c]: 10 bit index of vertex c (0-21) on diagonal d (0..maxd-1), into the code
    table at descriptor +0x26 (codes[]); odd diagonals use 21 vertices (c = 21 is 0).
    bases[d]: base height word per diagonal (0x18fd0, RLE at descriptor +0x0c).
    The original keeps diag in video RAM: pair p = d >> 1 at 0xa00000 + word 0x1eb3a[p]
    (playfield columns 39-63, then motion object RAM 0xa02400+), 10 bit fields MSB first,
    even diagonal fields 0-21, odd 22-43 (0x1a9cc)."""

    def __init__(self, race):
        rd = Rd(race)
        D = rd.desc
        self.race = race
        self.desc = D
        self.maxd = s16(rd.w(D + 0x18))
        count = s16(rd.w(D + 0x1a))
        bases = []
        a = rd.l(D + 0x0c)
        while True:
            n = rd.w(a); a += 2
            if n == 0: break
            v = rd.w(a); a += 2
            bases += [v] * n
        self.bases = bases
        code_base = rd.l(D + 0x26)
        ncodes = s16(rd.w(D + 0x24))
        code_end = code_base + 2 * ncodes - 2
        self.bank = rom_w(BANK_TERRAIN + 2 * race)
        rd.bank = self.bank
        self.codes = [rd.w(code_base + 2 * i) for i in range(ncodes)]
        stream = rd.l(D + 0x08)
        diag = []
        d4 = -0x18
        while True:
            d4 += 0x18
            m0c = s16(d4 + 0x15); d5 = s16((d4 >> 1) + 0x15)
            d6 = 0x18 if d4 <= self.maxd - 0x18 else self.maxd - d4
            S = [[0, 0, 0, 0] for _ in range(0x18 * 0x16 + 16)]
            rd.bank = PROG[BANK_RASTER + race]          # 0x2ffb8(byte 0x24994[race])
            ent = rd.l(D + 0x1c)
            bits = 0
            for i in range(count):
                if i & 15 == 0:
                    bits = rd.w(stream); stream += 2
                raster_1ad54(rd, S, ent + 8 * i, m0c, d5, d6, (bits >> (i & 15)) & 1)
            while True:                                 # single vertex overrides
                w = rd.w(stream); stream += 2
                if w & 0xfffe == 0xfffe: break
                S[(w >> 8) * 0x16 + (w & 0xff)][0] = rd.w(stream); stream += 2
            rd.bank = self.bank                         # 0x2ffb8(0x400662)
            for i in range(d6):
                diag.append(encode_1aa38(rd, S[i * 0x16:(i + 1) * 0x16], i & 1,
                                         s16(bases[d4 + i]), code_base, code_end))
            if w == 0xffff: break
        self.diag = diag
        self.vram = bytearray(0x3000)               # 0xa00000-0xa02fff, terrain pairs only
        for off, data in self.vram_image().items():
            self.vram[off:off + 54] = data

    def index(self, d, f):
        """Vertex field f (0-43, odd diagonals +0x16) of the diagonal pair holding d."""
        pair = self.diag[d & ~1] + self.diag[d | 1]
        if not 0 <= f < len(pair):
            raise IndexError("terrain field %d of diagonal %d" % (f, d))
        return pair[f]

    def vram_image(self):
        """{video RAM offset: 54 bytes} as written by 0x1a9cc."""
        img = {}
        for p in range(self.maxd // 2):
            off = s16(rom_w(0x1eb3a + 2 * p))
            bits = 0
            for v in self.diag[2 * p] + self.diag[2 * p + 1]:
                bits = bits << 10 | (v & 0x3ff)
            img[off] = (bits >> 8).to_bytes(54, "big")   # 440 bits, last 8 never stored
        return img


# ---------------------------------------------------------------- marble update (device side)

class Mem:
    """68k view: ROM tables, slapstic terrain bank, work RAM 0x400000-0x401fff."""
    def __init__(self, terrain, ram):
        self.t = terrain
        self.bank = terrain.bank
        self.ram = bytearray(ram)
        self.sounds = []
        self.events = []
        self.objects = False    # run the level object slots in tick()
        self.sprites = False    # projection 0x1b9cc and occlusion masks 0x264aa in tick()
        self.drawlist = None    # MO draw list hooks (marble_mo.DrawList): 0x18e6c / 0x18f46
        self.pair = None        # 0x1bc88 object pair collision and non-player landing hooks
        self.land = None        # (marble_enemy.Enemies), None: not ported
        self.deaths = False     # arcade death paths (0x25bae, 0x25c74) and dots 0x1c014
        self.enemies = False    # steelies (marble_enemy.enable): spawn, update, pair, landing
        self.slinkies = False   # slinkies (marble_slinky.enable): spawn, update, contact
        self.acid = False       # acid (marble_acid.enable): spawn, update, contact, state 9
        self.birds = False      # silly birds (marble_bird.enable): flag, update, contact, 0xb
        self.minis = False      # silly minis (marble_mini.enable): spawn/clear, update, contact
        self.pistons = False    # aerial pistons (marble_piston.enable): arm, update, contact
        self.aerial = False     # aerial objects (marble_aerial.enable): tags 0x0a-0x0d, state 0xa
    def _loc(self, a):
        a &= 0xffffff
        if a < 0x30000: return PROG, a, False
        if 0x80000 <= a < 0x88000: return ROM.slap, self.bank * 0x2000 + (a & 0x1fff), False
        if 0x400000 <= a < 0x402000: return self.ram, a - 0x400000, True
        if 0xa00000 <= a < 0xa03000: return self.t.vram, a - 0xa00000, False
        raise ValueError("addr %06x" % a)
    def rb(self, a):
        b, o, _ = self._loc(a); return b[o]
    def rw(self, a): return self.rb(a) << 8 | self.rb(a + 1)
    def rl(self, a): return self.rw(a) << 16 | self.rw(a + 2)
    def wb(self, a, v):
        b, o, w = self._loc(a); assert w; b[o] = v & 0xff
    def ww(self, a, v): self.wb(a, v >> 8); self.wb(a + 1, v)
    def wl(self, a, v): self.ww(a, (v >> 16) & 0xffff); self.ww(a + 2, v & 0xffff)
    def sb(self, a): return s8(self.rb(a))
    def sw(self, a): return s16(self.rw(a))
    def sl(self, a): return s32(self.rl(a))


def is_player(a): return a in (0x400018, 0x4000fa)


# 0x1bb50: derive cell / fraction fields from 0x400690/0x400692
def derive_1bb50(m):
    x = m.rw(0x400690); y = m.rw(0x400692)
    m.ww(0x40069e, x & 7); m.ww(0x4006a0, y & 7)
    m.ww(0x400696, (s16(x) >> 3) & 0xffff); m.ww(0x400698, (s16(y) >> 3) & 0xffff)
    m.ww(0x4006a2, 1 if (y & 7) >= (x & 7) else 0)

# 0x1bab2: copy object x/y/z words to 0x400690.. and refetch terrain on cell change
def posupd_1bab2(m, a):
    ox = m.rw(0x400696); oy = m.rw(0x400698)
    m.ww(0x400690, m.rw(a + 0xc)); m.ww(0x400692, m.rw(a + 0x10)); m.ww(0x400694, m.rw(a + 0x14))
    derive_1bb50(m)
    if m.rw(0x400696) != ox or m.rw(0x400698) != oy:
        terrain_1caba(m)


# 0x1caba: fetch the 4 vertex records around the cell (0x400696, 0x400698) into 0x401c28.
# Diagonal d = cx + cy + 1 - 0x15, column c = 0x15 - cx + d / 2; steps from table 0x24b3a.
def terrain_1caba(m):
    t = m.t
    a5 = 0x401c28
    tx = m.rw(0x400696)
    d4 = (m.rw(0x400698) + 1 + tx - 0x15) & 0xffff
    d6 = (0x15 - tx + (s16(d4) >> 1)) & 0xffff
    a6 = 0x24b3a + (0x12 if d4 & 1 else 0)
    for _ in range(4):
        d = s16(d4)
        if d < 0 or t.maxd <= d:
            vals = [0, 0, 0, 0]
        else:
            idx = t.index(d, s16(d6) + (0x16 if d & 1 else 0))
            vals = code_heights(m, t.codes[idx], t.bases[d])
        for k in range(4): m.ww(a5 + 2 * k, vals[k])
        a5 += 8
        st1 = m.rw(a6); st2 = m.rw(a6 + 2); st3 = m.rw(a6 + 4); a6 += 6
        d6 = (d6 + st1) & 0xffff
        d4 = (d4 + st2) & 0xffff
        a5 += s16(st3)


def code_heights(m, code, base):
    """Terrain code (0x1cb64 dispatch) -> 4 vertex heights, one per quadrant. 0 = no floor."""
    while True:
        if code >= 0xf000:                          # flat: base + (code & 0x7f) - 0x40
            v = ((code & 0x7f) - 0x40 + base) & 0xffff
            return [v, v, v, v]
        if code >= 0x1000:                          # two levels, bits 12-15 pick the high one
            v0 = ((code & 0x7f) - 0x40 + base) & 0xffff
            coef = m.rw(0x1ed62 + ((s16(code) >> 6) & 0x3e))
            alt = 0 if coef == 0x1000 else (s16(v0) - s16(coef)) & 0xffff
            return [v0 if code & (1 << b) else alt for b in (12, 13, 14, 15)]
        if code >= 0x800:                           # dynamic: RAM table 0x40076e (Phase 4)
            code = m.rw(0x40076e + (code & 0x7fe))
            continue
        if code == 0:
            return [0, 0, 0, 0]
        rec = (s16(code) + m.rl(m.t.desc)) & 0xffffff   # 4 byte record at desc +0x00
        d1 = (base - 0x80) & 0xffff
        return [0 if b == 0 else (b + d1) & 0xffff for b in (m.rb(rec + k) for k in range(4))]


# 0x1cc62: height under the marble, gradient to 0x4006a4/0x4006a6
def height_1cc62(m):
    b = 0x401c28
    cx0 = m.rw(b + 4); cx1 = m.rw(b + 0xe); cy0 = m.rw(b + 0x10); cz = m.rw(b + 0x1a)
    if m.rw(0x4006a2):
        dx = (cx1 - cx0) & 0xffff; dy = (cx0 - cz) & 0xffff
    else:
        dx = (cy0 - cz) & 0xffff; dy = (cx1 - cy0) & 0xffff
    m.ww(0x4006a4, dx); m.ww(0x4006a6, dy)
    d2 = s32(s16(cz) << 16)
    d1 = s32(s16(dy) * s16(m.rw(0x4006a0)) + s16(dx) * s16(m.rw(0x40069e)))
    d1 = s32(d1 << 13)
    return s32(d2 + d1)

# 0x1c676: edge/wall heights around the cell
def edges_1c676(m):
    for a in (0x40066a, 0x400672, 0x400670, 0x40066e, 0x40066c): m.wb(a, 0)
    S = [[m.rw(0x401c28 + 8 * i + 2 * k) for k in range(4)] for i in range(4)]
    s1, s2, s3, s4 = S
    fa = m.rw(0x4006a0); fb = m.rw(0x40069e)
    def br(out, dir_, key, hi, t1, t2, lo, hi2, probe, dlo, pivot, l1h, l1l, l3h, l3l, f):
        if key == hi and t1 == t2: return
        if s16(key) < s16(hi) or s16(probe) < s16(dlo):
            d = 1; o = lo
        else:
            d = 3; o = hi2
        m.ww(out, o)
        if pivot == hi: d += 1
        m.wb(dir_, d)
        if d == 1:
            diff = s16(s16(l1h) - s16(l1l))
            m.ww(out, (m.rw(out) + (s32(diff * s16(f) + 4) >> 3)) & 0xffff)
        elif d == 3:
            diff = s16(s16(l3h) - s16(l3l))
            m.ww(out, (m.rw(out) + (s32(diff * s16(f) + 4) >> 3)) & 0xffff)
    br(0x400674, 0x40066c, s1[2], s1[3], s4[1], s4[0], s4[0], s4[1], s4[1], s4[0], s4[0], s1[3], s4[0], s1[2], s4[1], fa)
    br(0x400676, 0x40066e, s1[2], s1[1], s2[3], s2[0], s1[1], s1[2], s2[3], s2[0], s2[0], s2[0], s1[1], s2[3], s1[2], fb)
    br(0x400678, 0x400670, s3[0], s3[1], s2[3], s2[2], s2[2], s2[3], s2[3], s2[2], s2[2], s2[2], s3[1], s2[3], s3[0], fa)
    br(0x40067a, 0x400672, s3[0], s3[3], s4[1], s4[2], s3[3], s3[0], s4[1], s4[2], s4[2], s3[3], s4[2], s3[0], s4[1], fb)
    def mx(rec, k, others):
        return all(s16(rec[o]) < s16(rec[k]) for o in others)
    flags = 0
    checks = [(0x01, s1, 0, (2, 3, 1), 0x40067c), (0x02, s2, 1, (3, 0, 2), 0x40067e),
              (0x04, s3, 2, (0, 1, 3), 0x400680), (0x08, s4, 3, (1, 2, 0), 0x400682),
              (0x10, s1, 2, (0, 3, 1), 0x40067c), (0x20, s2, 3, (1, 0, 2), 0x40067e),
              (0x40, s3, 0, (2, 1, 3), 0x400680), (0x80, s4, 1, (3, 2, 0), 0x400682)]
    for bit, rec, k, oth, out in checks:
        if mx(rec, k, oth):
            flags |= bit; m.ww(out, rec[k])
    m.wb(0x40066a, flags)
    base = m.rw(0x400694)
    for a in range(0x400674, 0x400684, 2):
        m.ww(a, (m.rw(a) - base) & 0xffff)

# 0x1b5c2: wall bounce
def walls_1b5c2(m, a2):
    d3 = m.sl(a2); d4 = m.sl(a2 + 4); d3i, d4i = d3, d4
    d5 = s16(m.rw(0x40069a) - m.rw(0x400696))
    d6 = s16(m.rw(0x40069c) - m.rw(0x400698))
    wd2 = m.sw(0x40069e); wa0 = m.sw(0x4006a0)
    g = {k: m.sw(k) for k in range(0x400674, 0x400684, 2)}
    fPX, fPY, fNX, fNY = (m.sb(a) for a in (0x40066c, 0x40066e, 0x400670, 0x400672))
    bm = m.rb(0x40066a)
    ab = lambda v: -v if v < 0 else v
    ng = lambda v: -v if v > 0 else v
    if (fPX != 0 and fPX < 3 and wd2 < 4 and g[0x400674] >= 4) or (fNX > 2 and d5 == 1 and g[0x400678] >= 4):
        d3 = ab(d3)
    if (fPY != 0 and fPY < 3 and wa0 > 4 and g[0x400676] >= 4) or (fNY > 2 and d6 == -1 and g[0x40067a] >= 4):
        d4 = ng(d4)
    if (fNX != 0 and fNX < 3 and wd2 > 4 and g[0x400678] >= 4) or (fPX > 2 and d5 == -1 and g[0x400674] >= 4):
        d3 = ng(d3)
    if (fNY != 0 and fNY < 3 and wa0 < 4 and g[0x40067a] >= 4) or (fPY > 2 and d6 == 1 and g[0x400676] >= 4):
        d4 = ab(d4)
    if (bm & 1 and d5 != -1 and d6 != 1 and wd2 < 4 and wa0 > 4 and g[0x40067c] >= 4) or (bm & 0x40 and d5 != 0 and d6 != 0 and g[0x400680] >= 4):
        d4 = ng(d4); d3 = ab(d3)
    if (bm & 2 and d5 != 1 and d6 != 1 and wd2 > 4 and wa0 > 4 and g[0x40067e] >= 4) or (bm & 0x80 and d5 != 0 and d6 != 0 and g[0x400682] >= 4):
        d4 = ng(d4); d3 = ng(d3)
    if (bm & 4 and d5 != 1 and d6 != -1 and wd2 > 4 and wa0 < 4 and g[0x400680] >= 4) or (bm & 0x10 and d5 != 0 and d6 != 0 and g[0x40067c] >= 4):
        d4 = ab(d4); d3 = ng(d3)
    if (bm & 8 and d5 != -1 and d6 != -1 and wd2 < 4 and wa0 < 4 and g[0x400682] >= 4) or (bm & 0x20 and d5 != 0 and d6 != 0 and g[0x40067e] >= 4):
        d4 = ab(d4); d3 = ab(d3)
    if s32(d3) != d3i:
        m.wb(0x400666, 1); m.wl(a2 + 0xc, m.rl(0x400684)); m.wl(a2, d3 & 0xffffffff)
    if s32(d4) != d4i:
        m.wb(0x400668, 1); m.wl(a2 + 0x10, m.rl(0x400688)); m.wl(a2 + 4, d4 & 0xffffffff)
    if m.rb(0x400666) or m.rb(0x400668):
        m.sounds.append(0x42)

SPEED_TABLE = 0x2398c
WL160 = {0x00, 0x10, 0x12, 0x17, 0x18, 0x20, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b}

# 0x160f6: edge teetering / leaving the ground
def edge_160f6(m, a2, h):
    D2 = 0
    D3 = s16(m.rw(0x40069a) - m.rw(0x400696))
    D4 = s16(m.rw(0x40069c) - m.rw(0x400698))
    tx = m.sw(0x40069e); ty = m.sw(0x4006a0)
    def ok(v): return not (4 <= v) and not (-1 >= v)
    i = m.rb(0x40066c)
    if i != 0 and i < 3 and tx < 4 and D3 != 1 and ok(m.sw(0x400674)): D2 |= 1
    i = m.rb(0x40066e)
    if i != 0 and i < 3 and ty > 4 and D4 != -1 and ok(m.sw(0x400676)): D2 |= 2
    i = m.rb(0x400670)
    if i != 0 and i < 3 and tx > 4 and D3 != -1 and ok(m.sw(0x400678)): D2 |= 4
    i = m.rb(0x400672)
    if i != 0 and i < 3 and ty < 4 and D4 != 1 and ok(m.sw(0x40067a)): D2 |= 8
    dg = m.rb(0x40066a)
    if dg & 1 and tx < 4 and ty > 4 and D3 == -1 and D4 == 1 and m.sw(0x40067c) < 4: D2 |= 0x10
    if dg & 2 and tx > 4 and ty > 4 and D3 == 1 and D4 == 1 and m.sw(0x40067e) < 4: D2 |= 0x20
    if dg & 4 and tx > 4 and ty < 4 and D3 == 1 and D4 == -1 and m.sw(0x400680) < 4: D2 |= 0x40
    if dg & 8 and tx < 4 and ty < 4 and D3 == -1 and D4 == -1 and m.sw(0x400682) < 4: D2 |= 0x80
    cc = m.rb(a2 + 0x58)
    if D2 and cc in WL160:
        if m.rb(a2 + 0x36) == 0: m.wb(a2 + 0x37, 0)
        if (s8(m.rb(a2 + 0x37)) & 0xffff) == D2 or m.rb(a2 + 0x36) == 2:
            pass
        else:
            m.wb(a2 + 0x36, 1)
            m.ww(a2 + 0x2e, m.rw(0x400696)); m.ww(a2 + 0x30, m.rw(0x400698))
            m.wb(a2 + 0x37, D2); m.wl(a2 + 8, -0x6000 & 0xffffffff)
    elif not D2:
        if m.rb(a2 + 0x36) != 2:
            m.wb(a2 + 0x36, 0); m.wl(a2 + 8, 0)
            if s32(m.sl(a2 + 0x14) - h) > 0x60000:
                m.wb(a2 + 0x36, 2)
                m.ww(a2 + 0x2e, m.rw(0x400696)); m.ww(a2 + 0x30, m.rw(0x400698))
                m.wl(a2 + 8, -0x6000 & 0xffffffff)
                if cc not in (0x12, 0x20): m.sounds.append(0x45)
    if m.rb(a2 + 0x36) != 1: return
    D5 = abs(s16(m.rw(0x400696) - m.rw(a2 + 0x2e)))
    D6 = abs(s16(m.rw(0x400698) - m.rw(a2 + 0x30)))
    if D5 >= 2 or D6 >= 2: return
    mask = m.rb(a2 + 0x37)
    best = -1; bestv = 0
    TX = m.rw(0x40069e); TY = m.rw(0x4006a0)
    for bit in range(8):
        iso = (s8(mask) & 0xffff) & (1 << bit)
        D1w = D2w = 5
        if iso == 1:
            cur = m.rw(0x400674)
            if D5 == 0: D1w = TX
        elif iso == 2:
            cur = m.rw(0x400676)
            if D6 == 0: D1w = (7 - TY) & 0xffff
        elif iso == 4:
            cur = m.rw(0x400678)
            if D5 == 0: D1w = (7 - TX) & 0xffff
        elif iso == 8:
            cur = m.rw(0x40067a)
            if D6 == 0: D1w = TY
        elif iso == 0x10:
            cur = m.rw(0x40067c)
            if not (D5 == 0xffff or D6 == 1): D1w = TX; D2w = (7 - TY) & 0xffff
        elif iso == 0x20:
            cur = m.rw(0x40067e)
            if not (D5 == 1 or D6 == 1): D1w = (7 - TX) & 0xffff; D2w = (7 - TY) & 0xffff
        elif iso == 0x40:
            cur = m.rw(0x400680)
            if not (D5 == 1 or D6 == 0xffff): D1w = (7 - TX) & 0xffff; D2w = TY
        elif iso == 0x80:
            cur = m.rw(0x400682)
            if not (D5 == 0xffff or D6 == 0xffff): D1w = TX; D2w = TY
        else:
            continue
        if D1w < 5:
            if D1w > D2w: ti = D1w
            elif D2w < 5: ti = D2w
            else: ti = D1w
        elif D2w < 5:
            ti = D2w
        else:
            continue
        mag = s8(m.rb(SPEED_TABLE + ti))
        if best == -1 or mag < best:                 # 0x165ec ble: the smallest (TS: largest)
            best = mag; bestv = cur
    if best == -1:
        m.wb(a2 + 0x36, 2); m.wl(a2 + 8, -0x6000 & 0xffffffff)
        if cc not in (0x12, 0x20): m.sounds.append(0x45)
    else:
        diff = s16(bestv) - best
        if diff > 0:
            m.wl(a2 + 0x14, (m.sl(a2 + 0x14) + (diff << 16)) & 0xffffffff)

# 0x1706c: velocity push at edges
def edgepush_1706c(m, a1):
    d5 = m.sl(a1); d6 = m.sl(a1 + 4)
    d3 = s8(m.rb(0x40069f)); d2 = s8(7 - m.rb(0x4006a1)); d1 = s8(7 - m.rb(0x40069f)); d4 = s8(m.rb(0x4006a1))
    T = lambda i: s16(m.rw(0x23d40 + 2 * i))
    mM4 = s16(T(d3)); mM2 = s16(-T(d2)); a4w = s16(-T(d1)); a0w = s16(T(d4))
    def fl(a):
        f = m.rb(a); return f != 0 and f < 3
    if fl(0x40066c) and d3 < 4 and m.sw(0x400674) > 0: d5 += mM4
    if fl(0x40066e) and d2 < 4 and m.sw(0x400676) > 0: d6 += mM2
    if fl(0x400670) and d1 < 4 and m.sw(0x400678) > 0: d5 += a4w
    if fl(0x400672) and d4 < 4 and m.sw(0x40067a) > 0: d6 += a0w
    bm = m.rb(0x40066a)
    if bm & 1 and d3 < 4 and d2 < 4 and m.sw(0x400674) > 0: d5 += mM4; d6 += mM2
    if bm & 2 and d2 < 4 and d1 < 4 and m.sw(0x400676) > 0: d6 += mM2; d5 += a4w
    if bm & 4 and d1 < 4 and d4 < 4 and m.sw(0x400678) > 0: d5 += a4w; d6 += a0w
    if bm & 8 and d4 < 4 and d3 < 4 and m.sw(0x40067a) > 0: d6 += a0w; d5 += mM4
    m.wl(a1, d5 & 0xffffffff); m.wl(a1 + 4, d6 & 0xffffffff)

# 0x25e7c: friction / vector scale
def friction_25e7c(m, a, mode):
    x = m.sl(a); y = m.sl(a + 4)
    d2 = abs(x) & 0xffffffff; d4 = abs(y) & 0xffffffff
    if d2 > d4: d3 = (((d4 >> 3) & 0xffff) * 3 + d2) & 0xffffffff
    else: d3 = (((d2 >> 3) & 0xffff) * 3 + d4) & 0xffffffff
    i4 = (d3 >> 15) & 0xf; i2 = (d3 >> 12) & 7
    t0 = s16(m.rw(0x1eef8 + 2 * i4)); t1 = s16(m.rw(0x1eef8 + 2 * i4 + 2))
    dd = s16(t1 - t0)
    d2l = s32((dd * i2) >> 3) + t0
    if d3 < 0x100: d3 = 0x100
    sec = None
    if mode == 2:
        d2l = s32(d2l << 2)
        d4f = 0 if (d2l & 0xffffffff) >= d3 else (d3 - d2l) & 0xffffffff
    elif mode == 3:
        d4f = 0 if (d2l & 0xffffffff) >= d3 else (d3 - d2l) & 0xffffffff
        d2l = s32(d2l + (d2l << 2))
        sec = 0 if (d2l & 0xffffffff) >= d3 else (d3 - d2l) & 0xffffffff
    elif mode == 4:
        d4f = ((d2l >> 2) + d3) & 0xffffffff
    else:
        d4f = 0 if (d2l & 0xffffffff) >= d3 else (d3 - d2l) & 0xffffffff
    div = (d3 >> 8) & 0xffff
    q = (((d4f << 6) & 0xffffffff) // div) & 0xffff
    q2 = (((sec << 6) & 0xffffffff) // div) & 0xffff if mode == 3 else q
    m.wl(a, s32(s16(x >> 8) * s16(q)) >> 6 & 0xffffffff)
    m.wl(a + 4, s32(s16(y >> 8) * s16(q2)) >> 6 & 0xffffffff)

# 0x25df6: gravity along the terrain gradient
def gravity_25df6(m, a):
    for g in (0x4006a4, 0x4006a6):
        v = m.sw(g)
        if abs(v) & 0xffff >= 0xc: m.ww(g, (v << 2) & 0xffff)
    gx = m.sw(0x4006a4) << 11; gy = m.sw(0x4006a6) << 11
    if m.rw(0x400394) == 4:
        m.wl(a, (m.sl(a) + gx) & 0xffffffff); m.wl(a + 4, (m.sl(a + 4) + gy) & 0xffffffff)
    else:
        m.wl(a, (m.sl(a) - gx) & 0xffffffff); m.wl(a + 4, (m.sl(a + 4) - gy) & 0xffffffff)

def friction_mode(c):
    if c in (0x2d, 0x2e, 0x3b): return 2
    if c in (0x38, 0x39, 0x3a): return 4
    if c in (0x2f, 0x30, 0x31): return 3
    if c in (0x10, 0x17, 0x18, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37): return -1
    return 0

# 0x121b8: object physics update
def physics_121b8(m, a2):
    m.wl(0x400684, m.rl(a2 + 0xc)); m.wl(0x400688, m.rl(a2 + 0x10)); m.wl(0x40068c, m.rl(a2 + 0x14))
    m.ww(0x40069a, (m.sl(0x400684) >> 19) & 0xffff); m.ww(0x40069c, (m.sl(0x400688) >> 19) & 0xffff)
    pl = is_player(a2)
    m.ww(0x400698, 0xffff); m.ww(0x400696, 0xffff)
    posupd_1bab2(m, a2)
    if s32(height_1cc62(m) - m.sl(a2 + 0x14)) > 0x100000:
        m.events.append("embedded")
        if not pl and m.enemies: lose_np(m, a2); return
        m.wb(a2 + 0x57, 0x65)
        if m.deaths: sound_15884(m); m.sounds.append(0x46); enter_25bae(m, a2, 4)
        else: m.wb(a2 + 0x1a, 4)
        return
    for o in (0, 4, 8):
        m.wl(a2 + 0xc + o, (m.sl(a2 + 0xc + o) + m.sl(a2 + o)) & 0xffffffff)
    posupd_1bab2(m, a2)
    edges_1c676(m)
    d4 = m.sl(a2); d5 = m.sl(a2 + 4)
    d1 = s16(m.rw(0x400692) - m.rw(0x400690) + 0x88)
    if (d1 < 4 and d4 > d5) or (d1 > 0x11c and d5 > d4):
        m.wl(a2, d5 & 0xffffffff); m.wl(a2 + 4, d4 & 0xffffffff)
        m.wl(a2 + 0xc, m.rl(0x400684)); m.wl(a2 + 0x10, m.rl(0x400688))
        h = (d5 + d4) >> 1
        m.wl(a2 + 0xc, (m.sl(a2 + 0xc) + h) & 0xffffffff); m.wl(a2 + 0x10, (m.sl(a2 + 0x10) + h) & 0xffffffff)
        posupd_1bab2(m, a2)
        m.events.append("side")
    m.wb(0x400668, 0); m.wb(0x400666, 0)
    walls_1b5c2(m, a2)
    if m.rb(0x400666) or m.rb(0x400668): posupd_1bab2(m, a2)
    m.wb(0x400668, 0); m.wb(0x400666, 0)
    slots_29cce(m, a2)
    if m.rb(a2 + 0x58) == 0xa or m.rb(a2 + 0x1a) == 4: return
    if m.pair and m.pair(m, a2): posupd_1bab2(m, a2)  # 0x1bc88 object pair collision
    if pl:
        if m.slinkies:
            import marble_slinky
            marble_slinky.contact_14e92(m, a2)
        if m.acid:
            import marble_acid
            if marble_acid.contact_175c8(m, a2): posupd_1bab2(m, a2)
        if m.pistons:
            import marble_piston
            if marble_piston.contact_1881c(m, a2): posupd_1bab2(m, a2)
        if m.minis:
            import marble_mini
            marble_mini.contact_1924e(m, a2)
        if m.birds:
            import marble_bird
            marble_bird.contact_19d94(m, a2)
        if m.rb(a2 + 0x1a) == 0xb: return
        zones_1365c(m, a2)
    h = height_1cc62(m)
    edge_160f6(m, a2, h)
    dz = abs(s32(m.sl(a2 + 0x14) - m.sl(0x40068c)))
    if dz > 0x80000 and m.rb(a2 + 0x58) == 0 and m.sl(a2 + 8) <= 0:
        m.wb(a2 + 0x36, 2); m.ww(a2 + 0x2e, m.rw(0x400696)); m.ww(a2 + 0x30, m.rw(0x400698))
        m.wl(a2 + 8, -0x6000 & 0xffffffff); m.sounds.append(0x45)
    if m.rb(a2 + 0x36) == 2:
        if h >= m.sl(a2 + 0x14):
            m.wb(a2 + 0x36, 0); m.wl(a2 + 8, 0)
            fall = (abs(s32(m.sl(a2 + 0x2a) - h)) >> 15)
            m.wl(a2 + 0x14, h & 0xffffffff)
            m.events.append(("land", s16(fall)))
            (land_25c74 if pl or not m.land else m.land)(m, a2, s16(fall))
        elif s32(m.sl(a2 + 0x2a) - m.sl(a2 + 0x14)) > 0x800000:
            m.events.append("fell")
            if not pl and m.enemies: lose_np(m, a2); return
            m.wb(a2 + 0x57, 0x64)
            if m.deaths: sound_15884(m); m.sounds.append(0x46); enter_25bae(m, a2, 4)
            else: m.wb(a2 + 0x1a, 4)
            return
    elif m.rb(a2 + 0x36) == 0:
        m.wl(a2 + 0x14, h & 0xffffffff)
    if m.sprites:                                    # no effect on the motion
        proj_1b9cc(m, a2)
        if m.deaths and pl and m.rb(a2 + 0x1a) not in (1, 5): dots_1c014(m, a2)
        enter_1281c(m, a2)
    edgepush_1706c(m, a2)
    if m.rb(a2 + 0x36): return
    fm = friction_mode(m.rb(a2 + 0x58))
    if fm != -1: friction_25e7c(m, a2, fm)
    if pl and m.rw(0x400390) != 1: gravity_25df6(m, a2)


def lose_np(m, a2):
    """Non-player embedded or fallen (helper-121b8.ts): 0x15bd0(a2, 1, 1), slot freed."""
    import marble_enemy
    marble_enemy.remove_15bd0(m, a2, 1, 1)


# 0x26196 / 0x261bc: speed cap
def speedcap_26196(m, a2):
    cap = 0x40000 if m.rb(a2 + 0x1a) == 0 else 0x50000
    d1 = abs(m.sl(a2)); d4 = abs(m.sl(a2 + 4))
    if d1 > d4: d3 = ((d4 >> 3) & 0xffff) * 3 + d1
    else: d3 = ((d1 >> 3) & 0xffff) * 3 + d4
    d3 &= 0xffffffff
    if m.deaths and is_player(a2): distance_score(m, a2, d3)
    if cap < d3:
        q = ((cap << 6) // ((d3 >> 8) & 0xffff)) & 0xffff
        m.wl(a2, (s32(s16(m.sl(a2) >> 8) * s16(q)) >> 6) & 0xffffffff)
        m.wl(a2 + 4, (s32(s16(m.sl(a2 + 4) >> 8) * s16(q)) >> 6) & 0xffffffff)

def distance_score(m, a2, n):
    """0x26214: +0xc4 sums the friction table 0x1eef8 at speed n (>> 9); every 0xa0 scores
    10 (0x28608: +0xbc, flag 0x40039c)."""
    i4 = (n >> 15) & 0xf; i2 = (n >> 12) & 7
    t0 = s16(rom_w(0x1eef8 + 2 * i4)); t1 = rom_w(0x1eef8 + 2 * i4 + 2)
    d1 = (((s16(t1 - t0) * i2) >> 3) + t0) >> 9
    m.ww(a2 + 0xc4, m.rw(a2 + 0xc4) + d1)
    if m.sw(a2 + 0xc4) > 0xa0:
        m.ww(a2 + 0xc4, m.rw(a2 + 0xc4) - 0xa0)
        m.wl(a2 + 0xbc, m.rl(a2 + 0xbc) + 10)
        m.wb(0x40039c, m.rb(0x40039c) | (1 << m.rb(a2 + 0x19)))


SCORES, SCORE_TEXTS = 0x23cd4, 0x23cf6   # 0x285b0: points (words), score text (longs)


def score_285b0(m, a2, k):
    """0x285b0 (helper-285b0.ts): player a2 scores points k (0x28608: +0xbc, flag 0x40039c),
    +0xd4 = its score text, +0x68/+0x69/+0x70/+0xd8 show it."""
    m.wl(a2 + 0xbc, (m.rl(a2 + 0xbc) + s16(rom_w(SCORES + 2 * k))) & 0xffffffff)
    m.wb(0x40039c, m.rb(0x40039c) | ((1 << (m.rb(a2 + 0x19) & 0x3f)) & 0xff))
    m.wl(a2 + 0xd4, rom_l(SCORE_TEXTS + 4 * k))
    m.wb(a2 + 0x70, 0); m.wb(a2 + 0x68, 0); m.wb(a2 + 0x69, 0xff); m.wb(a2 + 0xd8, 1)


# 0x17f66: input -> velocity
def input_17f66(m, a2):
    if m.rb(a2 + 0x18) in (2, 3): return
    c = m.rb(a2 + 0x58)
    if m.rb(a2 + 0x36) != 2 and c in (0, 0x3b, 0x2d, 0x2e, 0x38, 0x39, 0x3a, 0x2f, 0x30, 0x31):
        d3 = s8(m.rb(a2 + 0xc7)) * 0x160
        d2 = -s8(m.rb(a2 + 0xc6)) * 0x160
        if m.rb(a2 + 0x1a) in (1, 5):
            d1 = 0x1f - s8(m.rb(a2 + 0x56))
            if m.rb(a2 + 0x1a) == 5 and d1 < 4: d1 = 4
            d3 = s32(s16(d3 >> 8) * s16(d1) << 3)
            d2 = s32(s16(d2 >> 8) * s16(d1) << 3)
        m.wl(a2, (m.sl(a2) + d3) & 0xffffffff); m.wl(a2 + 4, (m.sl(a2 + 4) + d2) & 0xffffffff)
    elif m.rb(a2 + 0x36):
        vz = m.sl(a2 + 8) - 0x6000
        if vz < -0x50000: vz = -0x50000
        m.wl(a2 + 8, vz & 0xffffffff)
    speedcap_26196(m, a2)

# 0x253bc
def ground_253bc(m, a):
    if m.rb(a + 0x36) == 0:
        m.ww(a + 0x32, (m.sl(a + 0xc) >> 19) & 0xffff); m.ww(a + 0x34, (m.sl(a + 0x10) >> 19) & 0xffff)
        m.wl(a + 0x2a, m.rl(a + 0x14)); m.wb(a + 0x1d, m.rb(a + 0x1b))

# 0x29cce: collision with level object slots (0x400a9c, 25 x 0x56). Ported: wall
# segments (tags 0x1a-0x1f), round posts (0x05, Marble Love sub-29cce.ts, no trace contact),
# cell rectangles that set the surface code +0x58 and the tubes 0x12-0x16, 0x20-0x27 (a lost
# marble returns from 0x29cce early, skipping the bounce epilogue). Not ported (MARBLE.md
# "Level objects"): 0x0a catapult, 0x0b/0x0d aerial gates, 0x0c.
WALLS = {  # tag: (along x?, along range, perp range, end ranges)
    0x1b: (True, (-8, 0x20), (-0xc, 0), ((-8, 0), (0x18, 0x20))),
    0x1c: (True, (-8, 0x20), (-8, 4), ((-8, 0), (0x18, 0x20))),
    0x1e: (True, (-0xc, 0x5e), (-0xe, 0), ((-0xc, -4), (0x56, 0x5e))),
    0x1a: (False, (-8, 0x20), (-0xc, 0), ((-8, 0), (0x18, 0x20))),
    0x1d: (False, (-8, 0x20), (-8, 4), ((-8, 0), (0x18, 0x20))),
    0x1f: (False, (-0xc, 0x54), (-0xe, 0), ((-0xc, -4), (0x4c, 0x54))),
}
RECTS = {  # tag: (w, h, same z) : 0 <= slot.cx - cx < w, 0 <= slot.cy - cy < h
    0x10: (0x10, 0xe, False), 0x32: (4, 2, False), 0x33: (2, 4, False), 0x34: (8, 0xb, False),
    0x35: (3, 8, False), 0x36: (6, 3, False), 0x37: (2, 3, False),
    0x2d: (0x14, 0xe, True), 0x2e: (2, 3, True), 0x3b: (2, 3, True), 0x38: (0x14, 0xe, True),
    0x39: (0xc, 0xe, True), 0x3a: (4, 2, True), 0x2f: (0xa, 0x14, True), 0x30: (0x13, 9, True),
    0x31: (2, 6, True),
}
NOOP = {0x6, 0x7, 0x8, 0x9, 0xe, 0xf, 0x11, 0x19, 0x28, 0x29, 0x2a, 0x2b, 0x2c}
KEEP58 = {0, 0x10, 0x3b, 0x17, 0x18, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x2d, 0x2e, 0x38, 0x39, 0x3a, 0x2f, 0x30, 0x31}
QUIET = {0x10, 0x17, 0x18, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37}

# Tubes (Marble Love sub-29cce.ts runTube12..runTube26Or27, helper-1cd00.ts). An entry
# (0x12 beginner, 0x13/0x14 beginner, 0x20 intermediate, 0x22 silly funnel, 0x25 ultimate)
# teleports the marble to a fixed exit (the second exit if 0x17cb8 finds the first taken),
# state 3; the other tags are tube walls (3D shape collision 0x1cd00, shape index k).
SHAPES = 0x24c5e            # 0x1cd00 shape pointers (7 longs): 16 byte plane entries
TUBE_EXITS = (0x2dc, 0x2ac)  # 0x22 exit x words (y 0x2b0), first by rng(2), the other if blocked
TUBE_WALLS = {  # tag: (d6 range, a0 range, z word below or None, shape)
    0x15: ((-0x18, 0x28), (-0x18, 0x28), 0x3f40, 1),
    0x16: ((-0x10, 0x20), (-0x10, 0x18), None, 3),
    0x23: ((-0x10, 0x20), (-0x18, 0x28), None, 5), 0x24: ((-0x10, 0x20), (-0x18, 0x28), None, 5),
    0x26: ((-0x18, 0x28), (-0x18, 0x28), None, 6), 0x27: ((-0x18, 0x28), (-0x18, 0x28), None, 6),
}
TUBE_HOLES = {  # tag: (z above: entry, z at or above: walls shape 0/4, at or above: stop, exit)
    0x12: (0x3f40, 0x3f78, 0x3f60, 0),
    0x20: (0x3f80, 0x3fb0, 0x3fa0, 0),
}


def tube_exit(m, a2, s, x, y, sound=0x36, global_z=True, tag=True):
    """Teleport: marble at (x, y), z of its cell floor (global_z: also 0x40068c), tag: +0x58
    = the slot tag, state 3 (+0x59 ticks), sounds 0x15884 and sound."""
    m.wl(a2 + 0xc, (x & 0xffff) << 16); m.wl(a2 + 0x10, (y & 0xffff) << 16)
    posupd_1bab2(m, a2)
    z = height_1cc62(m) & 0xffffffff
    m.wl(a2 + 0x14, z)
    if global_z: m.wl(0x40068c, z)
    if tag: m.wb(a2 + 0x58, m.rb(s + 0x1f))
    m.wb(a2 + 0x1a, 3)
    m.sounds.append(sound); m.events.append(("tube", hex(m.rb(s + 0x1f))))


def tube_hole(m, a2, s, tag, d1, d2, z):
    """0x12 / 0x20: a hole over cells d1 -2..2, d2 -1..4 of the slot. In cells 0..2 / 0..3
    the surface is the tag; above the wall level shape 0 (1: lost); between the stop level
    and it the marble stops (it drops); below the stop level it leaves by the exit."""
    if not (-2 <= d1 < 3 and -1 <= d2 < 5) or z <= TUBE_HOLES[tag][0]: return 0
    _, walls, stop, _ = TUBE_HOLES[tag]
    if 0 <= d1 < 3 and 0 <= d2 < 4: m.wb(a2 + 0x58, tag); m.wb(a2 + 0x59, 0xff)
    if z >= walls: return shape_1cd00(m, a2, s, 0)
    if d1 < 0: return 0
    if z >= stop:
        m.wl(a2 + 4, 0); m.wl(a2, 0); return 0
    if tag == 0x12:
        if not is_player(a2):
            import marble_enemy
            marble_enemy.remove_15bd0(m, a2, 1, 1); return 0
        m.wb(a2 + 0x36, 0); m.wb(a2 + 0x59, 0x12)
        x = 0x204 if blocked_17cb8(m, a2, 0x20c, 0x264, 0x70) else 0x20c
        m.wl(a2, 0x40000); m.wl(a2 + 4, 0)
        tube_exit(m, a2, s, x, 0x264, 0x35, False, False)
        return 0
    m.wb(a2 + 0x36, 0); m.wb(a2 + 0x59, 0x12)
    m.wl(a2, 0); m.wl(a2 + 4, 0xfffc0000)
    y = 0x25c if blocked_17cb8(m, a2, 0x26c, 0x254, 0x70) else 0x254
    tube_exit(m, a2, s, 0x26c, y, 0x35, False)
    return 0


def tube_13(m, a2, s, d1, d2, d6, a0):
    """0x13 / 0x14 beginner: entering the slot row (d2 0, d1 0..2) leaves by the exit at
    (0x29c, 0x2e4) (0x2dc if taken) with v = 4 px down; else the mouth walls, shape 2."""
    if not (-0x18 <= d6 < 0x20 and -0x10 <= a0 < 0x18): return 0
    if d2 == 0 and 0 <= d1 < 3:
        m.wb(a2 + 0x59, 0x12)
        y = 0x2dc if blocked_17cb8(m, a2, 0x29c, 0x2e4, 0x70) else 0x2e4
        m.wl(a2, 0); m.wl(a2 + 4, 0x40000)
        tube_exit(m, a2, s, 0x29c, y, 0x35, False)
        return 0
    return shape_1cd00(m, a2, s, 2)


def tube_25(m, a2, s, d1, d2, z):
    """0x25 ultimate: on cells 0..1 of the slot below 0x3fe0 the marble leaves by an exit
    at x 0xf0, y 0x100 or 0x140 (rng(2), the other if taken), v = 4 px right."""
    if not (0 <= d1 <= 1 and 0 <= d2 <= 1) or z >= 0x3fe0: return
    m.wb(a2 + 0x36, 0); m.sounds.append(0x46); m.wb(a2 + 0x59, 0x12)
    a, b = (0x100, 0x140) if rng_13a98(m, 2) else (0x140, 0x100)
    y = b if blocked_17cb8(m, a2, 0xf0, a, 0x70) else a
    m.wl(a2, 0x40000); m.wl(a2 + 4, 0)
    tube_exit(m, a2, s, 0xf0, y, 0x35, False)


def tube_22(m, a2, s, d1, d2, z):
    """Funnel: on the cell before the slot cell (d1 = d2 = 1) a marble above 0x3f94 drops
    out of an exit (v = -4 px y, +0x59 0x12); on the funnel floor 0x3f74 it is lifted."""
    if d1 != 1 or d2 != 1: return
    if z > 0x3f94:
        m.wb(a2 + 0x36, 0); m.wb(a2 + 0x59, 0x12)
        m.wl(a2, 0); m.wl(a2 + 4, 0xfffc0000)
        a, b = TUBE_EXITS if rng_13a98(m, 2) else TUBE_EXITS[::-1]
        x = b if blocked_17cb8(m, a2, a, 0x2b0, 0x70) else a
        tube_exit(m, a2, s, x, 0x2b0)
        return
    if z == 0x3f74: m.wl(a2 + 0x14, m.rl(a2 + 0x14) + 0x20000)
    m.wl(a2 + 8, m.rl(a2 + 8) + 0x9000)
    m.wl(a2 + 4, 0); m.wl(a2, 0); m.wb(a2 + 0x36, 2)


def shape_1cd00(m, a2, s, k):
    """3D shape collision of the marble against shape k placed at slot s (cell corner + 8):
    plane entries (normal nx, ny, nz words; box x, y, z min/max bytes; point cx, cy, cz;
    nx > 0x800 marks the last entry, nx - 0x1000). The new (0x400690) or old (0x400684)
    position in a box and the plane crossed or within 0x400 of it: deep, the velocity
    loses its normal part; shallow, a fall of more than 16 px loses the marble (1), else
    back to the old position with the velocity reversed. Returns 1 for a loss."""
    vx0, vy0, vz0 = m.sl(a2), m.sl(a2 + 4), m.sl(a2 + 8)
    rx = (m.sw(s + 0xc) + 8) & 0xffff; ry = (m.sw(s + 0x10) + 8) & 0xffff; rz = m.sw(s + 0x14)
    x1 = s16(rx - m.rw(0x400690)); y1 = s16(ry - m.rw(0x400692)); z1 = rz - m.sw(0x400694)
    x2 = s16(rx - m.rw(0x400684)); y2 = s16(ry - m.rw(0x400688)); z2 = s16(rz - m.rw(0x40068c))
    e = rom_l(SHAPES + 4 * k)
    while True:
        nx = s16(rom_w(e)); last = nx > 0x800
        if last: nx = s16(nx - 0x1000)
        ny, nz = s16(rom_w(e + 2)), s16(rom_w(e + 4))
        b = [s8(PROG[e + 6 + i]) for i in range(6)]
        cx, cy, cz = s8(PROG[e + 0xc]), s8(PROG[e + 0xd]), s8(PROG[e + 0xe])
        inside = lambda x, y, z: b[0] <= x <= b[1] and b[2] <= y <= b[3] and b[4] <= z <= b[5]
        if inside(x1, y1, z1) or inside(x2, y2, z2):
            d5 = s32(nz * s16(z1 - cz) + ny * s16(y1 - cy) + nx * s16(x1 - cx))
            d6 = s32(nz * s16(z2 - cz) + ny * s16(y2 - cy) + nx * s16(x2 - cx))
            if abs(d5) < 0x400 or abs(d6) < 0x400 or (d5 ^ d6) < 0:
                r = shape_hit(m, a2, nx, ny, nz, abs(d6), vx0, vy0, vz0)
                if r >= 0: return r
        e += 16
        if last: return 0


def shape_hit(m, a2, nx, ny, nz, ad6, vx0, vy0, vz0):
    """0x1cd00 response (L_01CF36): -1 continue with the next entry, else the result."""
    if ad6 >= 0x400:
        for o, g in ((0xc, 0x400684), (0x10, 0x400688), (0x14, 0x40068c)): m.wl(a2 + o, m.rl(g))
        vx, vy, vz = m.sl(a2), m.sl(a2 + 4), m.sl(a2 + 8)
        dot = s16(s32(ny * s16(vy >> 7) + nx * s16(vx >> 7) + nz * s16(vz >> 7)) >> 8)
        m.wl(a2, (vx - nx * dot) & 0xffffffff); m.wl(a2 + 4, (vy - ny * dot) & 0xffffffff)
        m.wl(a2 + 8, (vz - nz * dot) & 0xffffffff)
        return -1
    if m.rb(a2 + 0x36) == 2 and abs(s32(m.rl(a2 + 0x14) - m.rl(a2 + 0x2a))) > 0x100000:
        m.sounds.append(0x46)
        if is_player(a2):
            m.wb(a2 + 0x57, 0x64); enter_25bae(m, a2, 4)
        else:
            import marble_enemy
            marble_enemy.remove_15bd0(m, a2, 1, 1)
        return 1
    m.wl(a2, -vx0 & 0xffffffff); m.wl(a2 + 4, -vy0 & 0xffffffff); m.wl(a2 + 8, -vz0 & 0xffffffff)
    for o, g in ((0xc, 0x400684), (0x10, 0x400688), (0x14, 0x40068c)): m.wl(a2 + o, m.rl(g))
    return 0


def slots_29cce(m, a2):
    mcx = m.sw(0x400696); mcy = m.sw(0x400698); mx = m.sw(0x400690); my = m.sw(0x400692)
    mz = m.rw(0x400694)
    vx = m.sl(a2); vy = m.sl(a2 + 4)
    old58 = m.rb(a2 + 0x58); m.wb(a2 + 0x58, 0)
    for i in range(25):
        s = 0x400a9c + i * 0x56
        if not m.rb(s + 0x18): continue
        sx = m.sw(s + 0xc); sy = m.sw(s + 0x10)
        d6 = s16(sx - mx); a0 = s16(sy - my)
        d1 = s16((sx >> 3) - mcx); d2 = s16((sy >> 3) - mcy)
        tag = s8(m.rb(s + 0x1f))
        if 5 <= tag <= 0x3b:
            if tag in WALLS:
                ax, ar, pr, ends = WALLS[tag]
                al, pe, v = (d6, a0, vx) if ax else (a0, d6, vy)
                if ar[0] < al < ar[1] and pr[0] < pe < pr[1]:
                    if (ends[0][0] < al < ends[0][1] and v < 0) or (ends[1][0] < al < ends[1][1] and v > 0):
                        m.wb(0x400666 if ax else 0x400668, 1)
                    m.wb(0x400668 if ax else 0x400666, 1)
                    m.sounds.append(0x42); m.events.append(("slot wall", tag))
            elif tag in RECTS:
                w, h, zc = RECTS[tag]
                if 0 <= d1 < w and 0 <= d2 < h and (not zc or m.rl(s + 0x14) == m.rl(a2 + 0x14)):
                    m.wb(a2 + 0x58, tag); m.wb(a2 + 0x59, 0xff)
            elif tag == 0x05:                        # round post: |d| < 3.5 px reflects both axes
                ax_, ay_ = (abs(d6) << 4) & 0xffff, (abs(a0) << 4) & 0xffff
                dist = (((ay_ >> 3) * 3 + ax_) if ax_ > ay_ else ((ax_ >> 3) * 3 + ay_)) & 0xffff
                if dist < 0x38:
                    m.wb(0x400668, 1); m.wb(0x400666, 1); m.sounds.append(0x42)
            elif tag == 0x17:
                if d1 == 0 and -0x12 < d2 <= 0: m.wb(a2 + 0x58, tag); m.wb(a2 + 0x59, 0xff)
            elif tag == 0x18:
                if -0x12 < d1 <= 0 and d2 == 0: m.wb(a2 + 0x58, tag); m.wb(a2 + 0x59, 0xff)
            elif tag == 0x22:
                tube_22(m, a2, s, d1, d2, mz)
            elif tag in TUBE_HOLES:
                if tube_hole(m, a2, s, tag, d1, d2, mz): return  # loss: no epilogue
            elif tag in (0x13, 0x14):
                if tube_13(m, a2, s, d1, d2, d6, a0): return
            elif tag == 0x25:
                tube_25(m, a2, s, d1, d2, mz)
            elif tag == 0x21:                        # intermediate tube walls, by cell
                if -1 <= d1 < 4 and -1 <= d2 < 3 and mz < 0x3f60 and shape_1cd00(m, a2, s, 4):
                    return
            elif tag in TUBE_WALLS:
                (x0, x1), (y0, y1), below, k = TUBE_WALLS[tag]
                if x0 <= d6 < x1 and y0 <= a0 < y1 and (below is None or mz < below) \
                        and shape_1cd00(m, a2, s, k):
                    return                           # loss: no epilogue
            elif 0x0a <= tag <= 0x0d and m.aerial:
                import marble_aerial
                marble_aerial.contact(m, a2, s, tag, d6, a0)
            elif tag not in NOOP:
                m.events.append(("slot", hex(tag)))
        if m.rb(a2 + 0x58) not in KEEP58:
            break
    c = m.rb(a2 + 0x58)
    if c in QUIET:
        if old58 not in QUIET: m.sounds.append(0x43)
    elif old58 in QUIET:
        m.sounds.append(0x44)
    if m.rb(0x400666):
        m.wl(a2 + 0xc, m.rl(0x400684)); m.wl(a2, -m.sl(a2) & 0xffffffff)
    if m.rb(0x400668):
        m.wl(a2 + 0x10, m.rl(0x400688)); m.wl(a2 + 4, -m.sl(a2 + 4) & 0xffffffff)

# 0x25c74: landing (player path)
def land_25c74(m, a2, fall):
    prev = m.rb(a2 + 0x1a)
    if m.deaths and prev == 6: m.wb(a2 + 0x18, 3)
    m.wb(a2 + 0x1a, 1)
    d4 = s8(m.rb(a2 + 0x57)) + fall
    if d4 > 0x7f: d4 = 0x7f
    m.wb(a2 + 0x57, d4)
    s56 = s8(m.rb(a2 + 0x56)); s57 = s8(m.rb(a2 + 0x57))
    if s56 + ((s57 - s56) >> 1) > 0x1f:
        m.events.append("broken")
        if not m.deaths:
            m.wb(a2 + 0x1a, 2); return
        if prev in (1, 5):                           # broken while dizzy or eaten
            sound_15884(m)
            m.wb(a2 + 0x5f, 0); m.wb(a2 + 0x56, 1); m.wb(a2 + 0x60, 2)
            m.wl(a2 + 0x5a, ANIM_DIZZY_END)
        if -0x10 < m.sw(a2 + 0x20) < 0xf0:           # on screen: shatter, else lost
            enter_25bae(m, a2, 2)
        else:
            m.wb(a2 + 0x57, 0x64); enter_25bae(m, a2, 4)
        return
    if prev == 1: return
    m.wb(a2 + 0x5f, 0); m.wb(a2 + 0x60, 2); m.wl(a2 + 0x5a, 0x20faa); m.sounds.append(0x39)

# 0x1365c: zone lines (checkpoint zone +0x1b, goal zone 0xff)
def zones_1365c(m, a2):
    race = m.rw(0x400394)
    a0 = m.rl(0x1eb16 + 4 * race)
    ncx = m.sw(0x400696); ncy = m.sw(0x400698); ocx = m.sw(0x40069a); ocy = m.sw(0x40069c)
    if ncx == ocx and ncy == ocy: return
    d2 = m.rb(a2 + 0x1b)
    while True:
        b0 = m.sb(a0); b1 = m.sb(a0 + 1); end = m.sb(a0 + 2)
        horiz = b0 < 0
        X = abs(b0); Y = abs(b1)
        hit = (Y in (ncy, ocy)) if horiz else (X in (ncx, ocx))
        if hit:
            on_new = on_old = False
            for i in range(Y if not horiz else X, end + 1):
                p = (i, Y) if horiz else (X, i)
                if p == (ncx, ncy): on_new = True
                if p == (ocx, ocy): on_old = True
            if on_old and not on_new:
                side_a = (ncy <= Y) if horiz else not (ncx > X)
                m.wb(a2 + 0x1b, m.rb(a0 + 3) if side_a else m.rb(a0 + 4))
                if m.rb(a2 + 0x1b) == 0xff:
                    m.events.append("goal"); m.wb(a2 + 0x1a, 6); m.wb(a2 + 0x57, 0x1e)
                    if race == 0 and d2 in (3, 4):
                        m.wl(a2, 0); m.wl(a2 + 4, 0)
                    else:
                        m.wl(a2, (m.sb(0x1eb2e + race) << 16) & 0xffffffff)
                        m.wl(a2 + 4, (m.sb(0x1eb34 + race) << 16) & 0xffffffff)
                elif m.rb(a2 + 0x1b) == 4 and race == 3: zone4_slots(m)
                if m.rb(a2 + 0x1b) != d2:
                    m.events.append(("zone", m.rb(a2 + 0x1b)))
                    zone_hook_13966(m, a2)
                return
        if m.rb(a0 + 3) == 0xff and m.rb(a0 + 4) == 0xff: return
        a0 += 6

def zone4_slots(m):
    """0x13892: aerial zone 4 entry: zone wait slots (state 4) removed, open gates (state 2,
    tags 0x0b/0x0d) closed again (anim 0x1d752/0x1d798, script restart)."""
    for i in range(SLOT_N):
        s = slot(i)
        if m.rb(s + 0x18) != 1: continue
        if m.rb(s + 0x1a) == 4: slot_start_12f44(m, s, 1); continue
        if m.rb(s + 0x1a) != 2 or m.rb(s + 0x1f) not in (0xb, 0xd): continue
        m.wl(s + 0x36, 0x1d752 if m.rb(s + 0x1f) == 0xb else 0x1d798)
        m.sounds.append(("1ef5a", m.rb(s + 0x1b)))
        script_12896(m, s)


# ---------------------------------------------------------------- sprite position, occlusion masks

def proj_1b9cc(m, a2, refetch=False):
    """Screen position of an object: +0x1e sx, +0x20 sy (sy counts up from the screen
    bottom), older pairs at +0x22, +0x26 (only shifted when the position is new)."""
    if refetch: m.ww(0x400698, 0xffff); m.ww(0x400696, 0xffff)
    posupd_1bab2(m, a2)
    z = m.rw(a2 + 0x14)
    if m.rb(a2 + 0x58) in (0x2f, 0x30, 0x31) and m.rb(0x400691) & 1 and abs(m.sl(a2)) > 0x4000:
        z = (z + 2) & 0xffff                         # bumpy surface: 2 px jitter
    sx = (m.rw(0x400692) - m.rw(0x400690) + 0x88) & 0xffff
    sy = (m.rw(0x40097e) + z + 0x54 - ((m.sw(0x400690) + m.sw(0x400692)) >> 1)) & 0xffff
    packed = sx << 16 | sy
    if packed in (m.rl(a2 + 0x1e), m.rl(a2 + 0x22)): return
    m.wl(a2 + 0x26, m.rl(a2 + 0x22)); m.wl(a2 + 0x22, m.rl(a2 + 0x1e)); m.wl(a2 + 0x1e, packed)


def enter_1281c(m, a2):
    """+0x1c = on screen (-16 < sy < 256); visible objects get their occlusion masks."""
    m.wb(a2 + 0x1c, 0)
    sy = m.sw(a2 + 0x20)
    if sy <= -16 or sy >= 0x100: return
    m.wb(a2 + 0x1c, 1)
    occlusion_264aa(m, a2, 0 if is_player(a2) else 1)


def vertex_height_177f8(m, d, col, k):
    """Height (word) of the terrain vertex on diagonal d under screen column col: the higher
    of two quadrants, picked by k + flag 0x400988 (table 0x2417e). 0 = no floor."""
    desc = m.t.desc
    if s16(d) >= s16(m.rw(desc + 0x18)): return 0
    d3 = s16(k) + m.sl(0x400988)
    d0 = (s16((col + m.rw(0x40098a)) & 0xffff) >> 1) + 2
    if d & 1: d0 += 0x16
    # the field map 0x1ed0a is the identity (field d0 of the pair): read the terrain index,
    # as the remake does. Same value the video RAM image 0x1eb3a gives (validated by replays).
    # code table *0x40065a = descriptor +0x26, bases 0x400478 = Terrain.bases (level init)
    code = m.t.codes[m.t.index(d, d0)] if d0 < 43 else m.t.codes[0]
    base = m.t.bases[d]
    while True:
        if code & 0xf000:
            if code & 0xf000 & m.rw(0x24176 + s16((d3 * 2) & 0xffff)):
                return ((code & 0x7f) - 0x40 + base) & 0xffff
            bias = m.rw(0x1ed62 + ((code & 0xf80) >> 6))
            if bias == 0x1000: return 0
            return ((code & 0x7f) - 0x40 - s16(bias) + base) & 0xffff
        if code & 0xfff == 0: return 0
        if code & 0x800:
            code = m.rw(0x40076e + (code & 0x7fe)); continue
        rec = (m.rl(desc) + s16(code)) & 0xffffff
        tab = 0x2417e + 8 * (d3 & 0x1fffffff)
        hv = max(m.rb(rec + m.rl(tab)), m.rb(rec + m.rl(tab + 4)))
        if hv == 0: return 0
        return (hv - 0x80 + base) & 0xffff


# Converter hook: callable(race, ult_col, column, d) -> first edge diagonal >= d or -1. None
# reads the edge bitmaps of the program ROM (descriptor +0x2e) as the arcade does.
EDGES = None

MASK_FULL = 0x6ce0      # mask entry code: height 6 tiles, MO code 0x4e0 (all pen 1)


def occlusion_264aa(m, a2, d3):
    """Occlusion masks of an object (d3 0 player, 1 other): up to 5 entries (code, x, y)
    at +0x38, one per 8 px screen column under the sprite. Entry code: bits 12-15 MO
    height in tiles, bit 15 also hflip (span masks), bit 11 set, bits 0-10 MO code (bank
    lookup 0x0400 | code & 0x7ff). The MO builder turns them into priority MOs (pen 1 =
    playfield shows, the sprite under it is hidden). +0x1c = 0 when every column is covered.
    d3 2 slinky (+0x2c, frame box), 3 bird (+0x26, 3 entries, screen +0x20): no explicit
    occluders, +0x1c untouched."""
    base = a2 + {2: 0x2c, 3: 0x26}.get(d3, 0x38)
    out = [base]
    def emit(*words):
        for w in words:
            m.ww(out[0], w); out[0] += 2
    for i in range(3 if d3 == 3 else 5): m.ww(base + 6 * i, 0)
    if d3 == 0:
        if m.rb(a2 + 0x58) and m.rb(a2 + 0x59) != 0xff and m.rb(a2 + 0x58) != 0xa \
                and m.rb(a2 + 0x1a) not in (9, 0xb, 0xa):
            m.wb(a2 + 0x1c, 0); return
        if m.rb(a2 + 0x1a) in (4, 6, 0xa): return
    x = m.sl(a2 + 0xc); y = m.sl(a2 + 0x10)
    if d3 == 2:                                      # slinky: box of its current frame
        f = rom_l(m.rl(a2 + 0x58))
        ncol = 5 if s8(PROG[f + 2]) > 2 else 3
        top = s8(PROG[f + 3]) << 3
        col = s16(s8(PROG[f]) + m.rw(a2 + 0x28)) >> 3
        col += 1 if ncol == 3 else 2
        sy = s16(s8(PROG[f + 1]) + m.rw(a2 + 0x2a))
    elif d3 == 3:                                    # bird (disassembly 0x264aa, A1 = a2)
        col = s16(m.rw(a2 + 0x20) + 3) >> 3
        sy = s16(m.rw(a2 + 0x22) - 8)
        ncol, top = 3, 0x10
    else:
        sy = m.sw(a2 + 0x20); col = m.sw(a2 + 0x1e) >> 3
        ncol, top = 3, 0xe
        if d3 == 0 and m.rb(a2 + 0x1a) in (1, 2, 5, 9): ncol, top = 5, 0x20
    top = s16(top + sy)                              # sprite top line (counting up)
    diag = s16((y >> 19) + (x >> 19) - 0x15)         # diagonal of the cell
    diag2 = s16(((x + y) >> 19) - 0x15)              # diagonal of the pixel sum
    shape = 0x1eaba
    if diag != diag2: shape += 10
    elif (col ^ diag) & 1: shape += 5
    if ncol == 3: shape += 1; c0 = col - 1
    else: c0 = col - 2
    if c0 < 0: ncol += c0; shape -= c0; c0 = 0
    elif ncol + c0 > 0x24: ncol = 0x24 - c0
    desc = m.t.desc
    cam = s16(m.rw(0x40097e) - m.rw(desc + 0x10))
    covered = 0
    def full(c):
        nonlocal covered
        covered |= 1 << (c - c0)
        emit(MASK_FULL, (c << 3) & 0xffff, (top - 0x2f) & 0xffff)
    race = m.rw(0x400394)
    for c in range(c0, c0 + ncol):
        d = max(0, s8(PROG[shape + c - c0]) + diag)
        # explicit occluders per column and race (0x1ea2a): records of 5 words
        # (diagonal limit, depth, control, y, entry code)
        tab = rom_l(0x1ea2a + 4 * c); sel = rom_w(tab + 2 * race) if d3 < 2 else 0
        done = False
        if sel:
            r = tab + 2 * sel
            while True:
                lim = s16(rom_w(r))
                if lim == -1: break
                if lim < d: r += 10; continue
                if d < lim - s16(rom_w(r + 2)): break
                ctl = s16(rom_w(r + 4)); yy = s16(rom_w(r + 6) + cam); code = rom_w(r + 8)
                done = True
                if 0 <= ctl < 8:
                    if yy >= sy - ctl: full(c)
                    else: emit(code, c << 3, yy & 0xffff)
                elif ctl == 8:
                    if yy < top: emit(code, c << 3, yy & 0xffff)
                elif ctl > 0:
                    if yy <= top: emit(code, c << 3, yy & 0xffff)
                    else: d = ctl & 0xff; done = False
                elif ctl == -2:
                    if s16(rom_w(r + 8)) > (m.sl(a2 + 0x14) >> 16): full(c)
                elif not (d == lim and diag2 == lim):
                    emit(code, c << 3, yy & 0xffff)
                break
        if done: continue
        # first terrain edge diagonal >= d in this column (bitmap, descriptor +0x2e)
        if EDGES is None:
            bm = rom_l(desc + 0x2e + 4 * c + s16(m.rw(0x40045c))) + (d >> 3)
            mask = PROG[0x1eaca + (d & 7)] & PROG[bm]; d &= ~7; p = bm
            if mask == 0:
                p += 1
                while PROG[p] == 0: p += 1
                mask = PROG[p]; d += (p - bm) * 8
            b = 1
            while not mask & b: b <<= 1; d += 1
        else:
            # remake edge lists (converter): same result, no bitmap bytes; none at or past
            # the last diagonal leaves the column unmasked as in the remake
            d = EDGES(m.t.race, s16(m.rw(0x40045c)), c, d) if d < m.t.maxd else -1
            if d < 0: continue
        m.wl(0x400988, (c ^ d) & 1)
        sline = s16((d << 2) - m.rw(0x40097e))
        y0 = s16(vertex_height_177f8(m, d, c, 0) - sline)
        if y0 > top + 0x10: full(c); continue
        if y0 < sy - 0x10: continue
        m.wl(0x400988, m.rl(0x400988) ^ 1)
        sline = s16(sline + 4)
        y1 = s16(vertex_height_177f8(m, d + 1, c, 2) - sline)
        if y1 <= sy and y0 <= sy: continue
        if y1 >= top and y0 >= top: full(c); continue
        if m.rl(0x400988) == 0: y0, y1 = y1, y0
        if y0 <= y1:
            emit(rom_w(0x1ead2 + 2 * (y1 - y0)), c << 3, (rom_w(0x1eaf4 + 2 * (y1 - y0)) + y0) & 0xffff)
        else:
            emit((rom_w(0x1ead2 + 2 * (y0 - y1)) + 0x8000) & 0xffff, c << 3,
                 (rom_w(0x1eaf4 + 2 * (y0 - y1)) + y1) & 0xffff)
    allm = (1 << ncol) - 1
    if d3 >= 2: return
    if covered & allm == allm or (d3 == 0 and ncol == 5 and covered & 0xe == 0xe):
        m.wb(a2 + 0x1c, 0)


# ---------------------------------------------------------------- level object slots

SLOTS, SLOT_N, SLOT_SIZE = 0x400a9c, 25, 0x56
ANIM_NONE = 0x20c14     # empty animation (first long 0xffffffff)
ANIM_REQ = 0x400970     # animation frame record queued for 0x1344c (one per tick, last wins)


def slot(i): return SLOTS + i * SLOT_SIZE


def rng_13a98(m, n):
    """Random 0..n-1: 16 bit shift register 0x4003a6, one step per significant bit of n."""
    d0 = m.rw(0x4003a6); d1 = n & 0xffff; k = 0
    while True:
        d2 = ((d0 >> 8) ^ d0) & 0xff or 0x40
        d0 = ((d0 << 1) | (d2 >> 6 & 1)) & 0xffff     # asl.b #2 -> X = bit 6, roxl.w
        k += 1; d1 >>= 1
        if not d1: break
    m.ww(0x4003a6, d0)
    if n == 0: return d0
    d0 &= (1 << k) - 1
    while not s16(n) > s16(d0): d0 = (d0 - n) & 0xffff   # cmp.w, bgt: signed (n 0xa000)
    return d0


def slot_start_12f44(m, a, mode, script=0):
    """mode 0: spawn (+0x3a script, state 3); mode 1: remove."""
    if mode == 0:
        m.wl(a + 0x3a, script); m.wb(a + 0x1a, 3); m.wb(a + 0x18, 1)
        return
    if a == m.rl(0x400974): m.wl(0x400978, 0); m.wl(0x400974, 0)
    m.wb(a + 0x18, 0); m.wb(a + 0x1a, 0)
    if m.rb(a + 0x1f) == 6: m.wb(0x40075c, m.rb(0x40075c) - 1)
    if m.rb(a + 0x1e) != 1 and m.drawlist:     # 0x18f46: out of the MO draw list 0x4003bc
        m.drawlist.remove(m, m.rb(a + 0x1f), m.rb(a + 0x19))


def slot_free_12d6e(m):
    for i in range(SLOT_N):
        a = m.rl(0x1f016 + 4 * i)
        if not m.rb(a + 0x18): return a
    return None


def spawned_12dae(m, ent):
    """Entry already alive: an active slot runs its script (+0x3a), or a random group
    entry (script 0) while a tag 0x0c slot is active."""
    for i in range(SLOT_N):
        a = slot(i)
        if m.rb(a + 0x18) != 1: continue
        if m.rl(ent + 2) == m.rl(a + 0x3a): return True
        if m.rl(ent + 2) == 0 and m.rb(a + 0x1f) == 0xc: return True
    return False


def spawn_12dfa(m, old, new):
    """Camera band change (16 px, 0x144e4): spawn list 0x1dec0[race] entries (lo, hi,
    script) whose band lo or hi is entered from outside [lo, hi]; remove slots whose band
    range is left."""
    old = s8(old); new = s8(new)
    e = m.rl(0x1dec0 + 4 * m.rw(0x400394))
    while m.rb(e) != 0xff:
        lo, hi = s8(m.rb(e)), s8(m.rb(e + 1))
        if not spawned_12dae(m, e) and new in (lo, hi) and not lo <= old <= hi:
            scripts = [m.rl(e + 2)]
            if scripts[0] == 0:                      # random group of 4 (0x1ded8)
                g = 0x1ded8 + 16 * rng_13a98(m, 4)
                scripts = [m.rl(g + 4 * k) for k in range(4)]
            for sc in scripts:
                a = slot_free_12d6e(m)
                if a is None: break
                m.ww(a + 0x52, lo & 0xffff); m.ww(a + 0x54, hi & 0xffff)
                slot_start_12f44(m, a, 0, sc)
        e += 6
    for i in range(SLOT_N):
        a = slot(i)
        if not m.rb(a + 0x18): continue
        if (old == m.sw(a + 0x52) and new < m.sw(a + 0x52)) or \
                (old == m.sw(a + 0x54) and new > m.sw(a + 0x54)):
            slot_start_12f44(m, a, 1)


def camera_bands_144e4(m, old, new):
    """Camera move: band = (camera - descriptor +0x10) >> 4. Steelie spawner 0x15a12 (m.enemies),
    slinkies 0x14c46 (m.slinkies), acid 0x17346 (m.acid), then the slots 0x12dfa; the aerial
    extras are not modelled."""
    top = m.sw(m.t.desc + 0x10)
    a = s16(old - top) >> 4; b = s16(new - top) >> 4
    if (a & 0xff) == (b & 0xff): return
    if m.enemies:
        import marble_enemy
        marble_enemy.spawn_15a12(m, a, b)
    if m.slinkies:
        import marble_slinky
        marble_slinky.spawn_14c46(m, a, b)
    if m.acid:
        import marble_acid
        marble_acid.spawn_17346(m, a, b)
    spawn_12dfa(m, a, b)
    if m.minis and m.rw(0x400394) == 4:
        import marble_mini
        marble_mini.band_144e4(m, a, b)
    if m.birds and m.rw(0x400394) == 4:
        import marble_bird
        marble_bird.band_flag_144e4(m, a, b)


def anim_render_13334(m, a):
    """Slot animation output. Mode 1 (playfield patch) and 2 (MO + terrain) queue the
    current frame record for 0x1344c; modes 0/2 store the screen position +0x4e and the
    MO frame +0x42."""
    mode = m.rb(a + 0x1e)
    if mode in (1, 2):
        if m.rl(m.rl(a + 0x3e)) == 0xffffffff: return
        if mode == 1 or m.rb(a + 0x1a) in (1, 2):
            m.wl(ANIM_REQ, m.rl(a + 0x3e)); m.wl(0x400974, a)
            if mode == 1: return
    x, y = m.rw(a + 0xc), m.rw(a + 0x10)
    m.ww(0x400690, x); m.ww(0x400692, y)
    sx = (y - x + 0x88) & 0xffff
    sy = (m.rw(0x40097e) + m.rw(a + 0x14) + 0x54 - ((s16(x) + s16(y)) >> 1)) & 0xffff
    m.wl(a + 0x4e, sx << 16 | sy)
    if m.rb(a + 0x1f) == 6: wave_1d06a(m, s8(m.rb(a + 0x25)))
    # tag 3: 0x26b66 (palette/banner animation, not modelled)
    m.wl(a + 0x42, m.rl(a + 0x3e))


def wave_1d06a(m, n):
    """Intermediate wave (tag 6): rewrites up to 4 x 3 dynamic terrain codes (Marble Love
    terrain-wave-update-1d06a.ts, unverified: no trace reaches it)."""
    tabs = {0: 0x24d5c, 1: 0x24da8, 2: 0x24dce, 3: 0x24df4}
    d1 = s8(PROG[0x24e1a + n]) if n <= 0xc else n - 6
    src = 0x24c98 + ((((d1 - 7) & 3) + 7) << 2)
    ctl = 0x24c7a + (d1 >> 2); dst = DYN + (d1 >> 2) * 6
    for _ in range(4):
        c = s8(PROG[ctl])
        if c < 0: break
        k = s8(PROG[src]) * 2
        t = (0x24d10, 0x24cc4, tabs[c]) if c < 4 else (0x24d36, 0x24cea, 0x24d82)
        for j in range(3): m.ww(dst + 2 * j, rom_w(t[j] + k))
        src += 1; ctl += 1; dst += 6


def anim_next_132e0(m, a):
    """Next animation frame (+4, modes 1/2 +8); at the 0xffffffff end wrap to +0x46
    (state 2) or +0x4a and count +0x1c down (0 = loop forever). True: script continues."""
    m.wl(a + 0x3e, m.rl(a + 0x3e) + (8 if m.rb(a + 0x1e) else 4))
    if m.rl(m.rl(a + 0x3e)) != 0xffffffff: return False
    m.wl(a + 0x3e, m.rl(a + (0x46 if m.rb(a + 0x1a) == 2 else 0x4a)))
    if m.rw(a + 0x1c) == 0: return False
    m.ww(a + 0x1c, m.rw(a + 0x1c) - 1)
    return m.rw(a + 0x1c) == 0


def players(m):
    return [P + 0xe2 * k for k in range(m.rw(0x400396))]


def script_12896(m, a):
    """Slot byte code (pc +0x36), word opcodes 0-18; runs until an opcode yields."""
    def arg():
        v = m.rw(m.rl(a + 0x36)); m.wl(a + 0x36, m.rl(a + 0x36) + 2); return v
    while True:
        op = s16(arg())
        if op < 0 or op > 18: return
        if op == 0:                                  # init: position, mode, tag
            m.wl(a + 0xc, (s16(arg()) << 19) & 0xffffffff)
            m.wl(a + 0x10, (s16(arg()) << 19) & 0xffffffff)
            m.wl(a + 0x14, (s16(arg()) << 16) & 0xffffffff)
            m.wb(a + 0x1e, arg()); m.wb(a + 0x1f, arg())
            m.wl(a + 0x42, ANIM_NONE)
            if m.rb(a + 0x1e) == 1:                  # playfield patch at (col, row)
                m.ww(a + 0x26, arg()); m.ww(a + 0x28, arg())
                pc = m.rl(a + 0x36); m.wl(a + 0x46, m.rl(pc)); m.wl(a + 0x36, pc + 4)
                m.wl(a + 0x3e, m.rl(a + 0x46))
                anim_render_13334(m, a)
            else:
                m.wl(a + 0x3e, ANIM_NONE)
                if m.drawlist:                       # 0x18e6c: into the MO draw list 0x4003bc
                    m.drawlist.insert(m, m.rb(a + 0x1f), m.rb(a + 0x19))
                if m.rb(a + 0x1f) == 6:
                    m.ww(a + 0x52, 0); m.ww(a + 0x54, 0x500)
                    m.wb(0x40075e, 0); m.wb(0x40075c, m.rb(0x40075c) + 1); m.wb(a + 0x25, 0)
            if m.rb(a + 0x1f) == 3:
                m.wl(a + 0x4e, 0xfffffe00)
                m.wl(0x40044a, 0x211fe); m.wb(0x400456, 2)
                m.wl(0x40044e, 0x2126e); m.wb(0x400458, 3)
                m.wl(0x400452, 0x212b2); m.wb(0x40045a, 3)
        elif op in (1, 2):                           # animate (state 1: +0x4a, 2: +0x46)
            if op == 2 and m.rb(a + 0x1f) == 0xa: m.sounds.append(0x60)
            m.ww(a + 0x1c, arg()); m.wb(a + (0x21 if op == 1 else 0x23), arg())
            m.wl(a + 0x3e, m.rl(a + (0x4a if op == 1 else 0x46)))
            m.wb(a + (0x20 if op == 1 else 0x22), 0); m.wb(a + 0x1a, op)
            return
        elif op == 3:                                # wait +0x1c ticks (0 = forever)
            m.ww(a + 0x1c, arg()); m.wb(a + 0x1a, 0)
            return
        elif op in (4, 6):                           # loop start, count
            k = 0x24 if op == 4 else 0x25
            m.wb(a + k, arg()); m.wl(a + (0x2a if op == 4 else 0x2e), m.rl(a + 0x36))
        elif op in (5, 7):                           # loop end: count 0 = forever
            k = 0x24 if op == 5 else 0x25
            if m.rb(a + k):
                m.wb(a + k, m.rb(a + k) - 1)
                if m.rb(a + k) == 0: continue
            m.wl(a + 0x36, m.rl(a + (0x2a if op == 5 else 0x2e)))
        elif op == 8:                                # player within 3 cells: jump
            w = arg(); hit = False
            if w == 0:
                for p in players(m):
                    if m.rb(p + 0x18) != 1: continue
                    dx = s16(s32(m.rl(p + 0xc) - m.rl(a + 0xc)) >> 19)
                    dy = s16(s32(m.rl(p + 0x10) - m.rl(a + 0x10)) >> 19)
                    if 0 <= dx < 3 and 0 <= dy < 3:
                        hit = True; m.sounds.append(0x31)
            if hit: m.wl(a + 0x36, m.rl(m.rl(a + 0x36)))
            else: m.wl(a + 0x36, m.rl(a + 0x36) + 2)
        elif op == 9: m.wl(a + 0x36, m.rl(m.rl(a + 0x36)))
        elif op == 10:
            pc = m.rl(a + 0x36); m.wl(a + 0x36, m.rl(pc)); m.wl(a + 0x32, pc + 4)
        elif op == 11: m.wl(a + 0x36, m.rl(a + 0x32))
        elif op in (12, 13):                         # animation for state 2 / 1
            pc = m.rl(a + 0x36); v = m.rl(pc)
            m.wl(a + (0x46 if op == 12 else 0x4a), v); m.wl(a + 0x3e, v); m.wl(a + 0x36, pc + 4)
        elif op == 14:
            m.wl(a, (s16(arg()) << 8) & 0xffffffff); m.wl(a + 4, (s16(arg()) << 8) & 0xffffffff)
        elif op == 15:
            slot_start_12f44(m, a, 1); return
        elif op == 16:
            m.wl(a + 0xc, (m.rl(a + 0xc) + m.rl(a)) & 0xffffffff)
            m.wl(a + 0x10, (m.rl(a + 0x10) + m.rl(a + 4)) & 0xffffffff)
        elif op == 17:                               # wait for a player in zone
            m.wl(a + 0x32, m.rl(a + 0x36) - 2); m.wb(a + 0x1a, 4); m.wb(a + 0x1b, arg())
            return
        elif op == 18:                               # no player in zone (or zone+1 >= 0x1e): jump
            free = True
            for p in players(m):
                if m.rb(p + 0x18) != 1: continue
                if m.rb(a + 0x1b) == m.rb(p + 0x1b) or \
                        (s8(m.rb(p + 0x1b)) == s8(m.rb(a + 0x1b)) + 1 and s8(m.rb(p + 0x1b)) >= 0x1e):
                    free = False
            pc = m.rl(a + 0x36)
            if free: m.wl(a + 0x36, m.rl(pc)); m.sounds.append(("1ef5a", m.rb(a + 0x1b)))
            else: m.wl(a + 0x36, pc + 4)


def slot_step_13068(m, a):
    """One logic tick of a slot (state +0x1a: 0 wait, 1/2 animate, 3 start, 4 zone wait)."""
    if not m.rb(a + 0x18): return
    d3 = m.rb(a + 0x1a); d2 = False
    if m.rb(a + 0x1f) == 3: tag3_globals(m)
    while True:
        st = m.rb(a + 0x1a)
        if st == 3:
            m.wl(a + 0x36, m.rl(a + 0x3a))
            m.wl(a + 0x4a, ANIM_NONE); m.wl(a + 0x46, ANIM_NONE); m.wl(a + 0x3e, ANIM_NONE)
            script_12896(m, a)
        elif st == 0:
            if m.rw(a + 0x1c):
                m.ww(a + 0x1c, m.rw(a + 0x1c) - 1)
                if m.rw(a + 0x1c) == 0 and m.rb(a + 0x18) == 1: script_12896(m, a)
            d2 = True
        elif st == 4:
            m.wl(a + 0x3e, ANIM_NONE); d2 = True
        elif st == 1:
            if m.rb(a + 0x21): m.wb(a + 0x20, m.rb(a + 0x20) + 1)
            if m.rb(a + 0x21) == m.rb(a + 0x20):
                m.wb(a + 0x20, 0)
                m.wl(a + 0xc, (m.rl(a + 0xc) + m.rl(a)) & 0xffffffff)
                m.wl(a + 0x10, (m.rl(a + 0x10) + m.rl(a + 4)) & 0xffffffff)
                anim_render_13334(m, a)
                if anim_next_132e0(m, a): script_12896(m, a)
            else: d2 = True
        elif st == 2:
            if m.rb(a + 0x23): m.wb(a + 0x22, m.rb(a + 0x22) + 1)
            if m.rb(a + 0x23) == m.rb(a + 0x22):
                m.wb(a + 0x22, 0)
                if m.rb(a + 0x1f) == 6:
                    m.wb(a + 0x25, m.rb(a + 0x25) + 1)
                    if m.rb(a + 0x25) == 0x1e: m.wb(0x40075e, 1)
                if m.rb(a + 0x1f) == 0x19:
                    m.ww(0x40045c, (((m.rl(a + 0x3e) - m.rl(a + 0x46)) >> 3) * 0x90) & 0xffff)
                anim_render_13334(m, a)
                if anim_next_132e0(m, a): script_12896(m, a)
            else: d2 = True
        if m.rb(a + 0x1a) != d3 and m.rb(a + 0x1a) == 1 and not m.rb(a + 0x21):
            d3 = m.rb(a + 0x1a); continue
        break
    if m.rb(a + 0x1e) != 1 and d2: anim_render_13334(m, a)


def tag3_globals(m):
    """Tag 3 (beginner) steps three global animation pointers (0x40044a/e/52)."""
    def step(ptr, timer, reload, wrap):
        m.wb(timer, m.rb(timer) - 1)
        if m.rb(timer): return
        m.wb(timer, reload)
        m.wl(ptr, m.rl(ptr) + 4)
        if m.rl(m.rl(ptr)) == 0xffffffff: wrap()
    def w44a():
        if m.rl(m.rl(0x40044a) - 4) == 0x211f2:
            m.wl(0x40044a, 0x2121e); m.wb(0x400456, m.rb(0x400456) + 0xf)
        else:
            m.wl(0x40044a, 0x211fe); m.wb(0x400456, m.rb(0x400456) + 0x1e)
    step(0x40044a, 0x400456, 2, w44a)
    step(0x40044e, 0x400458, 3, lambda: m.wl(0x40044e, 0x2126e))
    step(0x400452, 0x40045a, 3, lambda: m.wl(0x400452, 0x212b2))


def slots_12fd0(m):
    """Per tick slot update (main loop, after the objects): intermediate wave trigger,
    dynamic code reload (0x11ac2) while a wave runs, then every slot in index order."""
    if m.rw(0x400394) == 2:
        for p in players(m):
            if m.rb(p + 0x18) == 1 and m.rb(0x40075e) and m.rb(p + 0x1b) in (9, 0xa):
                a = slot_free_12d6e(m)
                if a is not None: slot_start_12f44(m, a, 0, 0x1d854)
                break
    if m.rb(0x40075c):
        for k in range(0x42): m.ww(DYN + 2 * k, m.rw(0x1d370 + 2 * k))
    for i in range(SLOT_N): slot_step_13068(m, slot(i))


def anim_apply_1344c(m):
    """Next tick, before the camera: the queued frame record (long playfield tile block,
    long dynamic terrain block) is applied. Tile block (mode 1 only): bytes w, h, then
    w x h playfield words at column +0x26, race row +0x28 (visible rows only; tag 0x19
    decodes ultimate rows instead); not modelled here, the remake must patch its
    playfield. Terrain block: word first code slot, word count, words -> 0x40076e."""
    rec = m.rl(ANIM_REQ)
    if not rec: return
    # mode 1 with rows on screen: 0x400978 = record after the blit (not modelled)
    dyn = m.rl(rec + 4)
    if dyn:
        first, n = m.rw(dyn), m.rw(dyn + 2)
        for k in range(n): m.ww(DYN + 2 * (s16(first) + k), m.rw(dyn + 4 + 2 * k))
    m.wl(ANIM_REQ, 0)


def start_race(m, race):
    """Race start as the remake runs it (marble_physics::startRace): player start, the draw
    list with the player when m.drawlist is on, the level init of the slots, the time out
    byte. Ends at a tick boundary of main_tick; tick() users call snapshot_start after it."""
    start_259b4(m, race)
    if m.drawlist: m.drawlist.init(m)
    objects_init(m, race)
    m.wb(0x40076c, 1)                                # level init end: time out event armed,
    m.wb(0x400444, rng_13a98(m, 0x100))              # its random byte (0x10fb8)


def objects_init(m, race):
    """Level init 0x105a0-0x10614 (slot part): 0x400768 = 0xffff, 0x40076a = 0, intermediate
    dynamic codes (0x11ac2), camera at the race top (silly: bottom), spawn the bands the
    camera enters from 0x19 px outside (0x144e4), one slot update (0x12fd0), then 0x1344c."""
    desc = m.t.desc
    m.ww(0x400394, race); m.ww(0x400396, 1)
    m.ww(0x400768, 0xffff); m.wb(0x40076a, 0)
    # slot number +0x19: set once at boot by the slot array init (state of a cleared RAM here)
    for i in range(SLOT_N): m.wb(SLOTS + i * SLOT_SIZE + 0x19, i)
    if m.enemies:                                    # steelie numbers, set at boot as well
        for i, a in enumerate(STEELIES_ALL): m.wb(a + 0x19, i)
    if m.slinkies:                                   # slinky numbers (0x1f006 order)
        for i in range(4): m.wb(SLINKIES + SLINKY_SIZE * i + 0x19, i)
    if m.acid:                                       # acid numbers (0x1f07a order)
        for i in range(7): m.wb(0x401482 + 0x42 * i + 0x19, i)
    if m.minis:                                      # mini numbers (0x1f096 order)
        for i in range(9): m.wb(0x401890 + 0x28 * i + 0x19, i)
    if m.birds:                                      # bird numbers (0x1f0ba order)
        for i in range(10): m.wb(0x4019f8 + 0x38 * i + 0x19, i)
    cam = rom_w(desc + 0x10) + (rom_w(desc + 0x12) if race == 4 else 0)
    m.wl(0x40097c, cam)
    if race == 2:
        for k in range(0x42): m.ww(DYN + 2 * k, m.rw(0x1d370 + 2 * k))
    old = cam + 0x19 if race == 4 else cam - 0x19
    camera_bands_144e4(m, old & 0xffff, cam & 0xffff)
    slots_12fd0(m)
    anim_apply_1344c(m)


def tick(m, c6=None, c7=None, a2=P, camera=None):
    """Snapshot point (0x121b8 entry) to the next one. c6/c7: trackball deltas of the next tick.
    With m.objects the level object slots run too (0x12fd0 after the objects, then the next
    tick's 0x1344c and, given its new camera long, the camera band spawns 0x144e4)."""
    st = m.rb(a2 + 0x1a)  # state handler was picked before the physics update
    physics_121b8(m, a2)
    if st == 1 and m.rb(a2 + 0x1a) == 1:  # 0x2574c tail: dizzy countdown
        if m.rb(a2 + 0x57): m.wb(a2 + 0x57, m.rb(a2 + 0x57) - 1)
        if s8(m.rb(a2 + 0x57)) < s8(m.rb(a2 + 0x56)):
            m.wb(a2 + 0x56, m.rb(a2 + 0x56) - 1)
            if m.rb(a2 + 0x56) == 0:
                m.wb(a2 + 0x1a, 0); m.wl(a2 + 0x5a, 0)
        else:
            m.wb(a2 + 0x56, m.rb(a2 + 0x56) + 1)
    if m.objects:
        slots_12fd0(m)
        anim_apply_1344c(m)
        if camera is not None:
            old = m.rw(0x40097e)
            m.wl(0x40097c, camera)
            if (camera & 0xffff) != old: camera_bands_144e4(m, old, camera & 0xffff)
    if c6 is not None: m.wb(a2 + 0xc6, c6); m.wb(a2 + 0xc7, c7)
    if m.rb(a2 + 0x1a) in (0, 1):
        # 0x25fc2 animation script step (state 1) does not touch the motion
        ground_253bc(m, a2)
        input_17f66(m, a2)


DYN = 0x40076e          # dynamic terrain codes 0x0800-0x0fff, word per code
# First frame of the level object animation that writes the dynamic codes (0x13628: word
# slot, word count, count words to 0x40076e + 2 * slot): practice 9 slots, beginner 12,
# ultimate 112 (MAME write tap, all from PC 0x13638).
DYN_FRAMES = {0: 0x1699c, 1: 0x1b908, 5: 0x24e4e}


def dyn_init(m, race):
    """Dynamic terrain codes at race start. Intermediate: level init 0x105be calls 0x11ac2,
    66 words from 0x1d370. Practice, beginner, ultimate: a level object animation (Phase 4)
    writes them, held at its first frame. The arcade writes beginner and ultimate only when
    the objects spawn (camera near), 0 = no floor before. Aerial, silly: none."""
    if race == 2:
        for k in range(0x42): m.ww(DYN + 2 * k, m.rw(0x1d370 + 2 * k))
        return
    if race not in DYN_FRAMES: return
    a = DYN_FRAMES[race]
    slot, n = m.rw(a), m.rw(a + 2)
    for k in range(n): m.ww(DYN + 2 * (slot + k), m.rw(a + 4 + 2 * k))


def start_259b4(m, race, a2=P, player=0):
    """Race start of one player on a cleared work RAM, at a tick boundary of main_tick.
    0x259b4 physics fields only; sprite setup (0x25b40, 0x1b9cc, 0x1c014, 0x1281c, 0x18e6c)
    and the slot/animation bytes (+0x6a-+0x71, +0xc4, +0xcb, +0xd1, +0xd8) are skipped."""
    m.ww(0x400394, race)
    dyn_init(m, race)
    cell = m.rw(m.t.desc + 0x14 + 2 * player)          # high byte cell x, low byte cell y
    m.wl(a2 + 0xc, ((cell >> 8) << 19) + 0x40000)
    m.wl(a2 + 0x10, ((cell & 0xff) << 19) + 0x40000)
    m.ww(0x400698, 0xffff); m.ww(0x400696, 0xffff)
    posupd_1bab2(m, a2)
    m.wl(a2 + 0x14, height_1cc62(m) & 0xffffffff)
    m.wb(a2 + 0x1b, 0)
    for o in (0, 4, 8, 0x5a): m.wl(a2 + o, 0)
    for o in (0x56, 0x57, 0x1a, 0x36, 0x58): m.wb(a2 + o, 0)
    m.wb(a2 + 0x18, 1)
    if race == 3:                                       # aerial: starts on surface 0x17/0x18
        m.wb(a2 + 0x58, 0x18 if player else 0x17); m.wb(a2 + 0x59, 0xff)


def snapshot_start(m, a2=P):
    """From a race start to the snapshot point of tick() (0x121b8 entry): the first tick's
    0x253bc, 0x17f66 (deltas 0). main_tick runs them itself."""
    ground_253bc(m, a2)
    input_17f66(m, a2)


def begin_race(m, race):
    """The remake machine's race start (marble.cpp beginRace) on a cleared work RAM:
    start_race, projection 0x1b9cc, masks 0x1281c."""
    start_race(m, race)
    proj_1b9cc(m, P)
    enter_1281c(m, P)


# ---------------------------------------------------------------- player states, deaths
# Player states 1 (dizzy), 2 (shatter), 4 (lost: respawn sparkle) with m.deaths on.
# Handlers of the object dispatcher 0x253ec (jump table 0x254ba; Marble Love
# refresh-frame-10fce.ts fun253ECDispatch). States 5, 7, 9, 0xb need enemies (Phase 4b).

ANIM_DIZZY = 0x20faa        # landing wobble (+0x5a animation lists, long frame headers)
ANIM_DIZZY_LOOP = 0x20fb6   # dizzy, +0x56 > 6
ANIM_DIZZY_END = 0x20fd2    # dizzy, +0x56 <= 6
ANIM_SHATTER = 0x20fde      # state 2
ANIM_DISSOLVE = 0x21062     # state 9 (acid)
ANIM_SHARDS = 0x215f6       # state 2 overlay +0x62 from shatter frame 9 on (+0x67 = 1)
SHATTER_SHARDS = 9          # shatter frame index that starts the overlay
ORBIT_TICKS = {0x64: 0x30, 0x65: 0x18, 0x66: 0x24}  # sparkle length by +0x57 (fell, lost, bird)
SINE = 0x1eda2              # sine words, 0x4000 = 1, quarter = 0x64 entries
ORBIT_STEPS = 0x1ef32       # 8 x (s8 dx, s8 dy) per sparkle star
SPAWN_X, SPAWN_Y, SPAWN_ZONE = 0x400462, 0x400466, 0x400472
BLOCK_HIT = 0x40046a        # index of the object 0x17cb8 found
CHECKPOINTS = 0x1ef1a       # per race: (cx, cy, zone, pad), 0xff end
STEELIES, STEELIE_SIZE = 0x4009a4, 0x7c
STEELIES_ALL = (STEELIES, STEELIES + STEELIE_SIZE)
SLINKIES, SLINKY_SIZE = 0x401302, 0x60


def sound_15884(m):
    m.sounds.append(0x3a)
    if m.rw(0x400394) != 2: m.sounds.append(0x3b)


def enter_25bae(m, a2, code):
    """State entry: 2 shatter animation, 9 dissolve, 4 lost (respawn 0x2591a at once, the
    sparkle state 4 shows it). Velocity x/y cleared."""
    m.wl(a2 + 4, 0); m.wl(a2, 0)
    if m.rb(a2 + 0x1a) == 6: m.wb(a2 + 0x18, 3)
    if code == 2:
        m.sounds.append(0x38)
        m.wb(a2 + 0x5f, 0); m.wb(a2 + 0x60, 2); m.wl(a2 + 0x5a, ANIM_SHATTER)
        m.wb(a2 + 0x56, 2); m.wb(a2 + 0x1a, 2)
    elif code == 9:
        m.wb(a2 + 0x5f, 0); m.wb(a2 + 0x60, 4); m.wl(a2 + 0x5a, ANIM_DISSOLVE)
        m.wb(a2 + 0x1a, 9)
    elif code == 4:
        respawn_2591a(m, a2)
        m.sounds.append(0x3c if m.rb(a2 + 0x57) == 0x65 else 0x3d)
        m.wl(a2 + 0x5a, 0); m.wb(a2 + 0x1a, 4)
        m.ww(a2 + 0xd2, m.rw(a2 + 0xd2) + 1)        # death count


def anim_25fc2(m, a2):
    """Animation step of +0x5a (one frame per +0x60 ticks, counter +0x5f). At the list
    end: states 1/5 loop the dizzy lists, state 2 enters lost (+0x56 == 2) or restarts,
    others (9) enter lost."""
    st = m.rb(a2 + 0x1a)
    idx = lambda: ((m.rl(a2 + 0x5a) - ANIM_SHATTER) & 0xffffffff) >> 2 & 0xffff
    if st == 2 and ANIM_SHATTER < m.rl(a2 + 0x5a) < ANIM_SHATTER + 0x80 \
            and s16(idx()) > SHATTER_SHARDS:
        m.wb(a2 + 0x66, m.rb(a2 + 0x66) + 1)
        if m.rb(a2 + 0x66) == 1:
            m.wb(a2 + 0x66, 0); m.wl(a2 + 0x62, m.rl(a2 + 0x62) + 4)
    m.wb(a2 + 0x5f, m.rb(a2 + 0x5f) + 1)
    if s8(m.rb(a2 + 0x60)) > s8(m.rb(a2 + 0x5f)): return
    m.wb(a2 + 0x5f, 0); m.wl(a2 + 0x5a, m.rl(a2 + 0x5a) + 4)
    if st == 2 and idx() == SHATTER_SHARDS:
        m.wl(a2 + 0x62, ANIM_SHARDS); m.wb(a2 + 0x66, 0); m.wb(a2 + 0x67, 1)
        m.sounds.append(0x5f)
    if m.rl(m.rl(a2 + 0x5a)) != 0xffffffff: return
    st = m.rb(a2 + 0x1a)
    if st in (1, 5):
        m.wb(a2 + 0x5f, 0); m.wb(a2 + 0x60, 2)
        m.wl(a2 + 0x5a, ANIM_DIZZY_LOOP if s8(m.rb(a2 + 0x56)) > 6 else ANIM_DIZZY_END)
        return
    if st == 2 and m.rb(a2 + 0x56) == 0:
        m.wb(a2 + 0x5f, 0); m.wb(a2 + 0x60, 2); m.wl(a2 + 0x5a, ANIM_DIZZY_END)
        m.wb(a2 + 0x56, 1)
        return
    if st == 2 and m.rb(a2 + 0x56) == 1:
        sound_15884(m); enter_25bae(m, a2, 2)
        return
    if st == 2:
        m.wb(a2 + 0x67, 0)
        if is_player(a2): m.ww(a2 + 0xa4, 0)
        if m.rb(a2 + 0x18) == 2:                     # broken steelie: out of the draw list
            m.wb(a2 + 0x18, 0)
            if m.drawlist: m.drawlist.remove(m, 1 if is_player(a2) else 2, m.rb(a2 + 0x19))
            return
    m.wb(a2 + 0x57, 0x65); enter_25bae(m, a2, 4)


def blocked_17cb8(m, a2, x, y, r):
    """Another live object near pixel (x, y): both deltas below 32 px and octant distance
    (max + 3/8 min of the deltas * 16) below r. Scans the players (not a2), the steelies,
    the slinkies; returns 1, 2, 4 (index to 0x40046a) or 0."""
    groups = ((players(m), 1), ([STEELIES + STEELIE_SIZE * k for k in range(2)], 2),
              ([SLINKIES + SLINKY_SIZE * k for k in range(4)], 4))
    for objs, kind in groups:
        for i, a in enumerate(objs):
            if (kind == 1 and a == a2) or not m.rb(a + 0x18): continue
            dx = abs(s16(m.rw(a + 0xc) - x)); dy = abs(s16(m.rw(a + 0x10) - y))
            if dx >> 3 > 3 or dy >> 3 > 3: continue
            ex, ey = (dx << 4) & 0xffff, (dy << 4) & 0xffff
            if r > ((ey >> 3) * 3 + ex if ex > ey else (ex >> 3) * 3 + ey):
                m.wl(BLOCK_HIT, i)
                return kind
    return 0


def nearest_2637a(m, a2):
    """Nearest free checkpoint of the ground zone +0x1d to the last ground cell (+0x32/+0x34),
    octant distance of the cell deltas * 16 below 0x300 -> SPAWN_X/Y (pixel), SPAWN_ZONE."""
    a3 = rom_l(CHECKPOINTS + 4 * m.rw(0x400394))
    zone = s8(m.rb(a2 + 0x1d)) & 0xffff
    best = 0x300
    while PROG[a3] != 0xff:
        if PROG[a3 + 2] == zone:
            ex = (abs(s16(m.rw(a2 + 0x32) - PROG[a3])) << 4) & 0xffff
            ey = (abs(s16(m.rw(a2 + 0x34) - PROG[a3 + 1])) << 4) & 0xffff
            d = (ey >> 3) * 3 + ex if ex > ey else (ex >> 3) * 3 + ey
            x, y = PROG[a3] * 8 + 4, PROG[a3 + 1] * 8 + 4
            if not blocked_17cb8(m, a2, x, y, 0x180) and d < best:
                best = d
                m.wl(SPAWN_X, x); m.wl(SPAWN_Y, y); m.wb(SPAWN_ZONE, PROG[a3 + 2])
        a3 += 4


def spawn_262b2(m, a2):
    """Respawn point: nearest checkpoint, else (backwards from the list end) the first free
    one of zone +0x1d - 1. None: pixel (0, 0), zone 0xff."""
    m.wl(SPAWN_Y, 0); m.wl(SPAWN_X, 0); m.wb(SPAWN_ZONE, 0xff)
    nearest_2637a(m, a2)
    if m.rb(SPAWN_ZONE) != 0xff: return
    first = rom_l(CHECKPOINTS + 4 * m.rw(0x400394))
    a3 = first
    while PROG[a3] != 0xff: a3 += 4
    zone = s8(m.rb(a2 + 0x1d))
    if zone: zone -= 1
    while a3 != first - 4:                           # the 0xff end entry is tested too
        x, y = PROG[a3] * 8 + 4, PROG[a3 + 1] * 8 + 4
        if not blocked_17cb8(m, a2, x, y, 0x180) and PROG[a3 + 2] == zone & 0xffff:
            m.wl(SPAWN_X, x); m.wl(SPAWN_Y, y); m.wb(SPAWN_ZONE, PROG[a3 + 2])
            return
        a3 -= 4


def dots_init_25b40(m, a2):
    """Surface dot points (+0x74 x, +0x84 y, +0x94 z words, 8 each) to the rest pose."""
    m.wb(a2 + 0xca, 0)
    for i in range(8):
        m.ww(a2 + 0x74 + 2 * i, (s8(PROG[0x1d3f4 + i]) << 11) & 0xffff)
        m.ww(a2 + 0x84 + 2 * i, (s8(PROG[0x1d3fc + i]) << 11) & 0xffff)
        m.ww(a2 + 0x94 + 2 * i, 0)


def respawn_2591a(m, a2):
    """Object to the respawn point (0x262b2): position, z = terrain, zone, motion cleared,
    dot pose, projection, zone hook 0x13966."""
    spawn_262b2(m, a2)
    m.wl(a2 + 0xc, (m.rl(SPAWN_X) << 16) & 0xffffffff)
    m.wl(a2 + 0x10, (m.rl(SPAWN_Y) << 16) & 0xffffffff)
    m.ww(0x400698, 0xffff); m.ww(0x400696, 0xffff)
    posupd_1bab2(m, a2)
    m.wl(a2 + 0x14, height_1cc62(m) & 0xffffffff)
    m.wb(a2 + 0x1b, m.rb(SPAWN_ZONE))
    for o in (8, 4, 0): m.wl(a2 + o, 0)
    for o in (0x56, 0x36, 0x58): m.wb(a2 + o, 0)
    m.wl(a2 + 0x26, 0); m.wl(a2 + 0x22, 0)
    dots_init_25b40(m, a2)
    proj_1b9cc(m, a2)
    zone_hook_13966(m, a2)


def zone_hook_13966(m, a2):
    """Zone entry effects of silly (4) and aerial (3), from 0x1365c on a zone change and from
    the respawn: sound latches 0x40076a, aerial pistons 0x186ac and zone wait slots (tags
    0x0b/0x0d) restarted."""
    race = m.rw(0x400394); zone = m.rb(a2 + 0x1b); flags = 0x40076a
    if race == 4:
        if zone == 5 and not m.rb(flags) & 1:
            m.sounds.append(0x15); m.wb(flags, m.rb(flags) | 1)
        elif zone == 6 and not m.rb(flags) & 2:
            m.sounds.append(0x16); m.wb(flags, m.rb(flags) | 2)
    if race != 3: return
    if zone == 0x14:
        if not m.rb(flags) & 1: m.sounds.append(0xf); m.wb(flags, m.rb(flags) | 1)
    elif zone in (6, 7) and not m.rb(flags) & 2:
        m.sounds.append(0x12); m.wb(flags, m.rb(flags) | 2); m.ww(0x400768, 0x3c)
    if m.pistons:
        import marble_piston
        marble_piston.arm_186ac(m)
    else: m.events.append(("unported", 0x186ac))
    for i in range(SLOT_N):
        s = slot(i)
        if m.rb(s + 0x18) != 1 or m.rb(s + 0x1a) != 4 or m.rb(s + 0x1b) != zone: continue
        if m.rb(s + 0x1f) == 0xb: m.wl(s + 0x36, 0x1d766)
        if m.rb(s + 0x1f) == 0xd: m.wl(s + 0x36, 0x1d7ac)
        m.sounds.append(("1ef42", m.rb(s + 0x1b)))
        script_12896(m, s)


def sine_pair(d3):
    """(cos, sin) words of angle d3 (0..0x191, 0x192 = full turn), 0x4000 = 1."""
    t = lambda i: s16(rom_w(SINE + 2 * i))
    if d3 <= 0x64: return t(d3), t(0x64 - d3)
    if d3 < 0xc9: return s16(-t(0xc8 - d3)), t(d3 - 0x64)
    if d3 <= 0x12d: return s16(-t(d3 - 0xc9)), s16(-t(0x12d - d3))
    return t(0x191 - d3), s16(-t(d3 - 0x12d))


def orbit_13ade(m, a2):
    """Respawn sparkle: 8 stars (code 0x10b + k, screen x, y; no mask bit) circling the
    screen position +0x1e/+0x20 on a shrinking radius, the first 4 visible ones at +0xa4,
    the next 4 at +0x38. +0x57 counts down (start by ORBIT_TICKS); True when it reaches 0."""
    c = s8(m.rb(a2 + 0x57))
    if c in ORBIT_TICKS:
        m.wb(a2 + 0x57, ORBIT_TICKS[c]); m.ww(a2 + 0x2e, 0)
    d1 = s8(m.rb(a2 + 0x57))
    m.wb(a2 + 0x57, m.rb(a2 + 0x57) - 1)
    if m.rb(a2 + 0x1a) == 0xb: d1 = s16(0x24 - d1)
    rad = s16(d1) >> 1
    d3 = (m.rw(a2 + 0x2e) + 10) & 0xffff
    if s16(d3) >= 0x192: d3 = (d3 - 0x192) & 0xffff
    m.ww(a2 + 0x2e, d3)
    sx, sy = m.rw(a2 + 0x1e), m.rw(a2 + 0x20)
    for k in range(4):
        m.ww(a2 + 0x38 + 6 * k, 0); m.ww(a2 + 0xa4 + 6 * k, 0)
    n = 0
    for k in range(8):
        cs, sn = sine_pair(s16(d3))
        a1 = s16((rad * cs) >> 12)
        d0 = (rad * sn) >> 12
        x = s16(s8(PROG[ORBIT_STEPS + 2 * k]) + s16(d0 + sx - a1))
        y = s16(s8(PROG[ORBIT_STEPS + 2 * k + 1]) + s16(d1 + sy - ((s16(d0) + a1) >> 1)))
        if -8 < x < 0x120 and -8 < y < 0xf0:
            dst = a2 + 0xa4 + 6 * n if n < 4 else a2 + 0x38 + 6 * (n - 4)
            n += 1
            m.ww(dst, 0x10b + k); m.ww(dst + 2, x); m.ww(dst + 4, y)
        d3 = (d3 + 0x32) & 0xffff
        if s16(d3) >= 0x192: d3 = (d3 - 0x192) & 0xffff
    m.wb(a2 + 0x1c, 1)
    return m.rb(a2 + 0x57) == 0


# 0x1c014 surface dots (Marble Love sprite-rotate-1c014.ts): 8 points on the unit sphere
# (+0x74 x, +0x84 y, +0x94 z words, 0x4000 = 1) turned by the velocity each tick (2 of them
# renormalised per tick in turn, +0xca), then the 4 front ones as dot MOs at +0xa4.
LERP = 0x1ee6e              # 1 / sqrt table (0x1c014 normalisation)
DOT_ANGLE = 0x24ade         # dot MO code by the projected pair delta
DOT_DY = 0x24b2c            # dot y bias by |dy|


def muls(a, b): return s16(a) * s16(b)


def asr14w(v): return (v >> 14) & 0xffff


def lerp_1ee6e(arg):
    arg &= 0xffff
    i = s16(((arg >> 10) - 0xc) & 0xffff)
    v1 = rom_w(LERP + 2 * (i + 1)); v0 = rom_w(LERP + 2 * i)
    return (((((arg & 0x3ff) * s16(v1 - v0)) >> 10) + v0) & 0xffff)


def sine_lerp(d6):
    def one(a):
        i = s16(a) >> 6; f = a & 0x3f
        v1 = s16(rom_w(SINE + 2 * (i + 1))); v0 = s16(rom_w(SINE + 2 * i))
        return (s16(((s16(v1 - v0) * f) >> 6) & 0xffff) + v0) & 0xffff
    return one(d6 & 0xffff), one((0x1922 - d6) & 0xffff)


def dots_1c014(m, a2):
    if m.rb(a2 + 0x58) == 0xa:
        vx = vy = 0
    else:
        vx = (m.sl(a2) >> 4) & 0xffff; vy = (m.sl(a2 + 4) >> 4) & 0xffff
    ax, ay = abs(s16(vx)) & 0xffff, abs(s16(vy)) & 0xffff
    if ax == 0 and ay == 0:
        if m.rw(0x400394) != 4:
            dots_out(m, a2); return
        vx = vy = ax = ay = 0x10
    if ax and ay:
        big, small = (ax, ay) if ax > ay else (ay, ax)
        na = ((small << 14) // big) & 0xffff
        d3 = lerp_1ee6e(((((na * na) >> 14) & 0xffff) + 0x4000) & 0xffff)
        q = 0x10000000 // d3 if d3 else 0
        q = 0 if q > 0xffff else q
        if ax > ay: d4 = q; d5 = ((na * d4) >> 14) & 0xffff
        else: d5 = q; d4 = ((na * d5) >> 14) & 0xffff
        if s16(vx) >= 0: d4 = (-s16(d4)) & 0xffff
        if s16(vy) < 0: d5 = (-s16(d5)) & 0xffff
        d3 = ((big * d3) >> 14) & 0xffff
    else:
        if ax: d4, d3, d5 = 0x4000, ax, 0
        else: d4, d3, d5 = 0, ay, 0x4000
        if s16(vx) >= 0: d4 = (-s16(d4)) & 0xffff
        if s16(vy) < 0: d5 = (-s16(d5)) & 0xffff
    d6 = (d3 // 7) & 0xffff
    if m.rw(0x400394) == 4:
        t = (0x600 - d6) & 0xffff
        d6 = 0 if s16(t) < 0 else t
    sn, cs = sine_lerp(d6)
    d2m = asr14w(muls(d5, d5)); nc = asr14w(muls(d4, d4))
    a3m = ((asr14w(muls(sn, nc))) + d2m) & 0xffff
    d3m = asr14w(muls(d5, (0x4000 - sn) & 0xffff))
    d3f = asr14w(muls(d4, d3m))
    ne = asr14w(-muls(cs, d4))
    a4m = ((asr14w(muls(sn, d2m))) + nc) & 0xffff
    n10 = asr14w(muls(cs, d5))
    n16 = (-s16(ne)) & 0xffff; n14 = (-s16(n10)) & 0xffff
    for lp in (0, 1):
        o = 2 * lp
        p4, p6, p8 = m.rw(a2 + 0x74 + o), m.rw(a2 + 0x84 + o), m.rw(a2 + 0x94 + o)
        nx = ((muls(ne, p8) + muls(d3f, p6) + muls(a3m, p4)) >> 14) & 0xffff
        ny = ((muls(n10, p8) + muls(a4m, p6) + muls(d3f, p4)) >> 14) & 0xffff
        nz = ((muls(sn, p8) + muls(n14, p6) + muls(n16, p4)) >> 14) & 0xffff
        if s8(m.rb(a2 + 0xca)) == lp:
            sq = (muls(nz, nz) + muls(ny, ny) + muls(nx, nx)) & 0xffffffff
            if sq < 0x1f80000 or sq > 0x2080000:
                d7 = (sq >> 14) & 0xffff
                q = 0x2000000 // d7 if d7 else 0
                sc = lerp_1ee6e(0 if q > 0xffff else q)
                nx, ny, nz = asr14w(muls(sc, nx)), asr14w(muls(sc, ny)), asr14w(muls(sc, nz))
        m.ww(a2 + 0x74 + o, nx); m.ww(a2 + 0x84 + o, ny); m.ww(a2 + 0x94 + o, nz)
    p = a2 + 0x74                                    # points 2-7 mirror points 0 and 1
    for _ in range(4):
        a, b = s16(m.rw(p)), s16(m.rw(p + 2))
        for k, v in enumerate((-a, -b, b + a, -a - b, a - b, -a + b)):
            m.ww(p + 4 + 2 * k, v & 0xffff)
        p += 16
    m.wb(a2 + 0xca, 0 if m.rb(a2 + 0xca) + 1 >= 8 else m.rb(a2 + 0xca) + 1)
    dots_out(m, a2)


def dots_out(m, a2):
    """The 8 points projected around the screen position, sorted by x; pairs (0, 1) ..
    (6, 7) give 4 dot MOs (code by the pair delta, x, y) at +0xa4."""
    bx = m.sw(a2 + 0x1e); by = (m.rw(a2 + 0x20) + 7) & 0xffff
    kind = 0
    if m.rb(a2 + 0x1a) == 8:
        t = m.rl(m.rl(a2 + 0xcc))
        kind = 1 if t == 0x215c6 else 2 if t == 0x215ea else 0
    pts = []
    for i in range(8):
        px, py, pz = m.sw(a2 + 0x74 + 2 * i), m.sw(a2 + 0x84 + 2 * i), m.rw(a2 + 0x94 + 2 * i)
        if kind == 1: pz = (pz - (s16(pz) >> 2)) & 0xffff
        elif kind == 2: pz = (pz + (s16(pz) >> 1)) & 0xffff
        x = s16((((bx << 1) + ((py - px) >> 10)) >> 1) & 0xffff)
        y = s16((((s16(by) << 1) + ((s16(pz) - ((py + px) >> 1)) >> 10)) >> 1) & 0xffff)
        pts.append((x, y))
    for i in range(7):
        for j in range(i + 1, 8):
            if pts[i][0] > pts[j][0]: pts[i], pts[j] = pts[j], pts[i]
    for k in range(4):
        (xa, ya), (xb, yb) = pts[2 * k], pts[2 * k + 1]
        dx = min(s16(xb - xa), 2); dy = s16(yb - ya)
        ady = min(abs(dy), 0xc)
        code = rom_w(DOT_ANGLE + (((ady << 2) + dx - ady) * 2 & 0xffff))
        x, y = xa, ya
        if dy < 0:
            code = (code + 0x8000) & 0xffff; x = (xa + dx - 6) & 0xffff; y = (ya + dy) & 0xffff
        e = a2 + 0xa4 + 6 * k
        m.ww(e + 2, x)
        m.ww(e + 4, (s8(PROG[DOT_DY + ady]) + y) & 0xffff)
        m.ww(e, code)


def wobble_25416(m, a2):
    """+0xd8 squash wobble (silly minis): +0x68 swings -0x16..2, +0x70 counts odd steps."""
    m.wb(a2 + 0x68, m.rb(a2 + 0x68) + m.rb(a2 + 0x69))
    if s8(m.rb(a2 + 0x68)) > 2: m.wb(a2 + 0x68, 2); m.wb(a2 + 0x69, 0xff)
    elif s8(m.rb(a2 + 0x68)) < -0x16: m.wb(a2 + 0x68, 0xea); m.wb(a2 + 0x69, 1)
    if m.rb(a2 + 0x68) & 1:
        m.wb(a2 + 0x70, m.rb(a2 + 0x70) + 1)
        if s8(m.rb(a2 + 0x70)) > 0x28: m.wb(a2 + 0x70, 0xff); m.wb(a2 + 0xd8, 0)


def tube_25514(m, a2):
    """Player state 3 (refresh-frame-10fce.ts JT[3]): screen, dots, visibility; +0x59 counts
    down while +0x58 holds the tube tag, then points (0x12: 9; 0x13, 0x14, 0x20, 0x22, 0x25:
    5), +0x58 = 0, +0x22/+0x26 cleared, rolling again."""
    proj_1b9cc(m, a2, True); dots_1c014(m, a2); enter_1281c(m, a2)
    t = m.rb(a2 + 0x58)
    if not t: return
    m.wb(a2 + 0x59, m.rb(a2 + 0x59) - 1)
    if m.rb(a2 + 0x59): return
    if t == 0x12: score_285b0(m, a2, 9)
    elif t in (0x13, 0x14, 0x20, 0x22, 0x25): score_285b0(m, a2, 5)
    elif t == 0xa: m.sounds.append(0x45)
    m.wb(a2 + 0x58, 0); m.wl(a2 + 0x26, 0); m.wl(a2 + 0x22, 0); m.wb(a2 + 0x1a, 0)


def object_253ec(m, a2):
    """One player object tick by state +0x1a: 0 roll, 1 dizzy, 2 shatter, 4 lost, 6 goal.
    Other states (3, 5, 7-0xb) are not ported: event."""
    st = m.rb(a2 + 0x1a)
    if m.rb(a2 + 0xd8) and st not in (2, 4, 0xa, 0xb, 7): wobble_25416(m, a2)
    if st == 0:
        if m.rb(a2 + 0xcb) and not m.rb(a2 + 0x36) and m.rb(a2 + 0x58) in (0, 0x34, 0x35):
            m.events.append(("unported state", 8))   # 0x256f8: time out animation
            return
        ground_253bc(m, a2); input_17f66(m, a2); physics_121b8(m, a2)
    elif st == 1:
        anim_25fc2(m, a2); ground_253bc(m, a2); input_17f66(m, a2); physics_121b8(m, a2)
        if m.rb(a2 + 0x1a) != 1: return
        if m.rb(a2 + 0x57): m.wb(a2 + 0x57, m.rb(a2 + 0x57) - 1)
        if s8(m.rb(a2 + 0x57)) >= s8(m.rb(a2 + 0x56)):
            m.wb(a2 + 0x56, m.rb(a2 + 0x56) + 1); return
        m.wb(a2 + 0x56, m.rb(a2 + 0x56) - 1)
        if m.rb(a2 + 0x56): return
        sound_15884(m); m.wb(a2 + 0x1a, 0); m.wl(a2 + 0x5a, 0)
        dots_1c014(m, a2)
    elif st == 2:
        anim_25fc2(m, a2); proj_1b9cc(m, a2, True)
        if m.rb(a2 + 0x1c): enter_1281c(m, a2)
    elif st == 4:
        proj_1b9cc(m, a2, True)
        done = orbit_13ade(m, a2)
        if done or s8(m.rb(a2 + 0x57)) < 2:
            if blocked_17cb8(m, a2, m.sw(a2 + 0xc), m.sw(a2 + 0x10), 0x70):
                m.wb(a2 + 0x57, 0x65); enter_25bae(m, a2, 4); done = False
        if done:
            m.wl(a2 + 4, 0); m.wl(a2, 0); m.wb(a2 + 0x1a, 0)
    elif st == 5:
        # eaten wobble 0x257ba (refresh-frame-10fce.ts JT[5]): a slinky holds the marble
        # (0x14e92); +0x56 (50 from 0x14e92) counts down, then rolling again
        anim_25fc2(m, a2); ground_253bc(m, a2); input_17f66(m, a2); physics_121b8(m, a2)
        if m.rb(a2 + 0x1a) != 5: return
        if m.rb(a2 + 0x56): m.wb(a2 + 0x56, m.rb(a2 + 0x56) - 1)
        if m.rb(a2 + 0x56): return
        sound_15884(m); m.wb(a2 + 0x1a, 0); m.wl(a2 + 0x5a, 0)
        dots_1c014(m, a2)
    elif st == 7:
        # inside a slinky 0x25812 (JT[7]): hidden until the slinky's state 5 sends it lost
        ground_253bc(m, a2); m.wb(a2 + 0x1c, 0)
    elif st == 9 and m.acid:
        import marble_acid
        marble_acid.dissolve_2584e(m, a2)
    elif st == 3:
        tube_25514(m, a2)
    elif st == 0xb and m.birds:
        import marble_bird
        marble_bird.carried_25876(m, a2)
    elif st == 0xa and m.aerial:
        import marble_aerial
        marble_aerial.caught_2563e(m, a2)
    elif st == 6:
        # goal 0x254d2 (Marble Love refresh-frame-10fce.ts JT[6]): physics, +0x57 (0x1e from
        # 0x1365c) counts down; at 0 out of play (+0x18 = 3), v = 0. +0x57 wraps to 0xff, so
        # this repeats every 256 ticks while state 6 lasts.
        if m.rw(0x400390) == 1: m.events.append(("unported", 0x1815a))   # attract
        physics_121b8(m, a2)
        m.wb(a2 + 0x57, m.rb(a2 + 0x57) - 1)
        if m.rb(a2 + 0x57) == 0 and m.rb(a2 + 0x18) != 2:
            m.wb(a2 + 0x18, 3); m.wl(a2 + 4, 0); m.wl(a2, 0)
    else:
        m.events.append(("unported state", st))


def timeout_1bbaa(m):
    """Race with a time out event (0x40076c, race start rng 0x400444 below 0x24a94[race]): a
    rolling player with 3-6 s left in a zone of list 0x24aae[race] sets +0xcb of all."""
    race = m.rw(0x400394)
    zones = rom_l(0x24aae + 4 * race)
    if not m.rb(0x40076c) or PROG[0x24a94 + race] <= m.rb(0x400444) or PROG[zones] == 0xff:
        return
    for a in players(m):
        z = zones; hit = False
        while PROG[z] != 0xff:
            hit |= m.rb(a + 0x1b) == PROG[z]; z += 1
        if m.rb(a + 0x18) == 1 and m.rb(a + 0x1a) == 0 and 2 < m.sw(a + 0x6a) < 7 and hit:
            m.wb(0x40076c, 0)
            for b in players(m):
                if m.rb(b + 0x18) == 1: m.wb(b + 0xcb, 1)


def objects_251de(m):
    """Player objects 0x400018 + 0xe2 k in turn (one player: no 2 player respawn swap)."""
    timeout_1bbaa(m)
    for a2 in players(m):
        if m.rb(a2 + 0x18): object_253ec(m, a2)


def main_tick(m, c6, c7, camera, frame):
    """Main loop tick from its top (0x11804, mame_trace.lua MARBLE_TICK=start) to the next:
    inputs c6/c7 (trackball deltas the vblank IRQ left in +0xc6/+0xc7), camera long after
    0x13ee6 (an input as in objfixture), frame counter 0x400010 at the tick start. Runs
    0x1344c, the camera band spawns, the players 0x251de, the slots 0x12fd0, rng(0x100)
    0x118a4. Needs m.deaths, m.objects, m.sprites on."""
    m.wl(0x400010, frame)
    m.wb(P + 0xc6, c6); m.wb(P + 0xc7, c7)
    anim_apply_1344c(m)                              # 0x13ee6: 0x1344c, camera, 0x144e4
    old = m.rw(0x40097e)                             # 0x144e4 runs before the camera store
    if (camera & 0xffff) != old: camera_bands_144e4(m, old, camera & 0xffff)
    m.wl(0x40097c, camera)
    objects_251de(m)
    # 0x189e2 practice markers: not ported
    if m.enemies:
        import marble_enemy
        for a in STEELIES_ALL: marble_enemy.update_158f6(m, a)
    if m.slinkies:
        import marble_slinky
        marble_slinky.update_1493c(m)
    if m.acid:
        import marble_acid
        marble_acid.update_17230(m)
    if m.minis:
        import marble_mini
        marble_mini.update_1912c(m)
    if m.birds:
        import marble_bird
        marble_bird.update_19baa(m)
    if m.pistons:
        import marble_piston
        marble_piston.update_1844a(m)
    slots_12fd0(m)
    rng_13a98(m, 0x100)


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


def player_mos(m):
    """MOs 0x26f3e emits for player 1 (marble_mo.type1): [(w0, code, w2)]."""
    import marble_mo
    return marble_mo.type1(m, 0)


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


# ---------------------------------------------------------------- host test fixture replay

def load_fixture(path):
    """Segments of source/test/test_marble_physics/marble_trace.h (tools/make_fixture.py):
    [{"race", "frame", "ram": [(addr, bytes)], "ticks": [(c6, c7, x, y, z, state)]}]."""
    import re
    text = open(path).read()
    num = lambda s: [int(v, 0) for v in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", s)]
    arrays = {name: body for name, body in
              re.findall(r"static const \w+ (\w+)\[\] = \{(.*?)\n?\};", text, re.S)}
    segs = []
    for body in re.findall(r"\{ *(\d+), *(\w+), *(\d+), *(\w+), *(\d+), *(\w+) *\}",
                           arrays["marble_trace_segments"]):
        race, frame, nram, ramname, nticks, tickname = body
        ram = []
        for addr, length, data in re.findall(r"\{ *(0x[0-9a-f]+), *(\d+), *(\w+) *\}",
                                             arrays[ramname]):
            b = bytes(num(arrays[data]))
            assert len(b) == int(length)
            ram.append((int(addr, 0), b))
        v = num(arrays[tickname])
        ticks = [tuple(v[k:k + 6]) for k in range(0, len(v), 6)]
        assert len(ram) == int(nram) and len(ticks) == int(nticks)
        segs.append({"race": int(race), "frame": int(frame), "ram": ram, "ticks": ticks})
    return segs


def fixture_mem(seg, terrain, mem=Mem):
    """Work RAM of a fixture segment: zero except the listed initial blocks."""
    ram = bytearray(0x2000)
    for addr, b in seg["ram"]:
        ram[addr - 0x400000:addr - 0x400000 + len(b)] = b
    return mem(terrain, ram)


def replay_fixture(seg, m):
    """Run every fixture tick on m; index of the first tick that differs, or None."""
    for k, (c6, c7, x, y, z, state) in enumerate(seg["ticks"]):
        tick(m, c6 & 0xff, c7 & 0xff)
        if (m.sl(P + 0xc), m.sl(P + 0x10), m.sl(P + 0x14), m.rb(P + 0x1a)) != (x, y, z, state):
            return k
    return None


def cmd_fixture(path):
    ok = True
    for seg in load_fixture(path):
        bad = replay_fixture(seg, fixture_mem(seg, Terrain(seg["race"])))
        print("fixture race %d frame %d: %d ticks, %s" % (seg["race"], seg["frame"],
              len(seg["ticks"]), "exact" if bad is None else "differs at tick %d" % bad))
        ok &= bad is None
    return ok


OBJ_SLOT_AREA = (0xa9c, 0xa9c + SLOT_N * SLOT_SIZE)   # 0x400a9c-0x401301
OBJ_DYN_AREA = (0x76e, 0x86e)                           # 128 dynamic code words


def obj_state(m):
    """Fixture check values of one tick: player x, y, z, state, +0x1c, the 30 bytes of the
    mask entries (+0x38), crc32 of the slot area and of the dynamic code words (work RAM
    offsets OBJ_SLOT_AREA, OBJ_DYN_AREA, big-endian bytes as in the 68k RAM)."""
    import zlib
    a = P - 0x400000
    return (m.sl(P + 0xc), m.sl(P + 0x10), m.sl(P + 0x14), m.rb(P + 0x1a), m.rb(P + 0x1c),
            bytes(m.ram[a + 0x38:a + 0x56]), zlib.crc32(bytes(m.ram[slice(*OBJ_SLOT_AREA)])),
            zlib.crc32(bytes(m.ram[slice(*OBJ_DYN_AREA)])))


def load_objfixture(path):
    """Segments of source/test/test_marble_objects/marble_objects_trace.h:
    [{"race", "frame", "ram": [(addr, bytes)], "ticks": [(c6, c7, camera, x, y, z, state,
    vis, masks, slot_crc, dyn_crc)]}]."""
    import re
    text = open(path).read()
    num = lambda s: [int(v, 0) for v in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", s)]
    arrays = {name: body for name, body in
              re.findall(r"static const \w+ (\w+)\[\] = \{(.*?)\n?\};", text, re.S)}
    segs = []
    for race, frame, nram, ramname, nticks, tickname in re.findall(
            r"\{ *(\d+), *(\d+), *(\d+), *(\w+), *(\d+), *(\w+) *\}",
            arrays["marble_objects_segments"]):
        ram = []
        for addr, length, data in re.findall(r"\{ *(0x[0-9a-f]+), *(\d+), *(\w+) *\}",
                                             arrays[ramname]):
            b = bytes(num(arrays[data]))
            assert len(b) == int(length)
            ram.append((int(addr, 0), b))
        v = num(arrays[tickname])
        w = 8 + 30 + 2
        ticks = [tuple(v[k:k + 8]) + (bytes(v[k + 8:k + 38]),) + tuple(v[k + 38:k + 40])
                 for k in range(0, len(v), w)]
        assert len(ram) == int(nram) and len(ticks) == int(nticks)
        segs.append({"race": int(race), "frame": int(frame), "ram": ram, "ticks": ticks})
    return segs


def replay_objfixture(seg, m):
    """Run every object fixture tick on m (objects and sprites on); index of the first tick
    that differs, or None."""
    m.objects = m.sprites = True
    for k, (c6, c7, cam, x, y, z, state, vis, masks, scrc, dcrc) in enumerate(seg["ticks"]):
        tick(m, c6 & 0xff, c7 & 0xff, camera=cam & 0xffffffff)
        if obj_state(m) != (x, y, z, state, vis, masks, scrc, dcrc):
            return k
    return None


def cmd_objfixture(path):
    ok = True
    for seg in load_objfixture(path):
        bad = replay_objfixture(seg, fixture_mem(seg, Terrain(seg["race"])))
        print("objects fixture race %d frame %d: %d ticks, %s" % (seg["race"], seg["frame"],
              len(seg["ticks"]), "exact" if bad is None else "differs at tick %d" % bad))
        ok &= bad is None
    return ok


def death_state(m):
    """Death fixture check values of one tick: player x, y, z, state, +0x1c, sx, sy, +0x5a,
    the drawn frame header (rl(+0x5a) in states 1/2/5/9, else 0), the shard overlay header
    (rl(+0x62) when +0x67), mask entries +0x38 (30 bytes), RNG word, player MO words."""
    a = P - 0x400000
    drawn = m.rb(P + 0x1c)
    hdr = m.rl(m.rl(P + 0x5a)) if drawn and m.rb(P + 0x1a) in (1, 2, 5, 9) else 0
    shard = m.rl(m.rl(P + 0x62)) if drawn and m.rb(P + 0x67) else 0
    return (m.sl(P + 0xc), m.sl(P + 0x10), m.sl(P + 0x14), m.rb(P + 0x1a), drawn,
            m.sw(P + 0x1e), m.sw(P + 0x20), m.rl(P + 0x5a), hdr, shard,
            bytes(m.ram[a + 0x38:a + 0x56]), m.rw(0x4003a6), tuple(player_mos(m)))


def load_dfixture(path, rng_in=False, arr=None):
    """Segments of source/test/test_marble_deaths/marble_deaths_trace.h (make_fixture.py
    dwrite): [{"race", "frame", "ram", "ticks": [(c6, c7, camera, frames, state tuple)]}].
    rng_in: marble_tubes_trace.h (twrite), ticks (c6, c7, camera, frames, rng, state). arr:
    segment array name (default by rng_in; marble_aerials_segments for awrite)."""
    import re
    text = open(path).read()
    num = lambda s: [int(v, 0) for v in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", s)]
    arrays = {name: body for name, body in
              re.findall(r"static const \w+ (\w+)\[\] = \{(.*?)\n?\};", text, re.S)}
    segs = []
    for race, frame, nram, ramname, nticks, tickname, nmos, moname in re.findall(
            r"\{ *(\d+), *(\d+), *(\d+), *(\w+), *(\d+), *(\w+), *(\d+), *(\w+) *\}",
            arrays[arr or ("marble_tubes_segments" if rng_in else "marble_deaths_segments")]):
        ram = []
        for addr, length, data in re.findall(r"\{ *(0x[0-9a-f]+), *(\d+), *(\w+) *\}",
                                             arrays[ramname]):
            b = bytes(num(arrays[data]))
            assert len(b) == int(length)
            ram.append((int(addr, 0), b))
        v = num(arrays[tickname]); mw = num(arrays[moname])
        mos = [tuple(mw[k:k + 3]) for k in range(0, len(mw), 3)]
        w = 14 + 30 + 3 + rng_in
        ticks = []
        for k in range(0, len(v), w):
            t = v[k:k + w]
            inp, t = tuple(t[:4 + rng_in]), t[4 + rng_in:]
            x, y, z, st, vis, sx, sy, anim, hdr, shard = t[:10]
            rng, n, first = t[40:43]
            ticks.append(inp + ((x, y, z, st, vis, sx, sy, anim, hdr, shard,
                          bytes(t[10:40]), rng, tuple(mos[first:first + n])),))
        assert len(ram) == int(nram) and len(ticks) == int(nticks) and len(mos) == int(nmos)
        segs.append({"race": int(race), "frame": int(frame), "ram": ram, "ticks": ticks})
    return segs


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
