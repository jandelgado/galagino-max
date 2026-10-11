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
Level objects (0x400a9c slots), dynamic terrain codes (0x40076e) and enemies are Phase 4:
`run` copies those from the trace each tick, divergences at deaths and enemy contacts end a
segment and are reported, any other divergence fails.
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
    def _loc(self, a):
        a &= 0xffffff
        if a < 0x30000: return PROG, a, False
        if 0x80000 <= a < 0x88000: return ROM.slap, self.bank * 0x2000 + (a & 0x1fff), False
        if 0x400000 <= a < 0x402000: return self.ram, a - 0x400000, True
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
        if best == -1 or mag > best:
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
        m.events.append("embedded"); m.wb(a2 + 0x57, 0x65); m.wb(a2 + 0x1a, 4); return
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
    # 0x1bc88 object pair collision, 0x14e92/0x175c8/0x1881c/0x1924e/0x19d94: enemies, not ported
    if pl:
        zones_1365c(m, a2)
        if m.rb(a2 + 0x1a) == 0xb: return
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
            land_25c74(m, a2, s16(fall))
        elif s32(m.sl(a2 + 0x2a) - m.sl(a2 + 0x14)) > 0x800000:
            m.events.append("fell"); m.wb(a2 + 0x57, 0x64); m.wb(a2 + 0x1a, 4); return
    elif m.rb(a2 + 0x36) == 0:
        m.wl(a2 + 0x14, h & 0xffffffff)
    # 0x1b9cc / 0x1c014 / 0x1281c sprite: not ported
    edgepush_1706c(m, a2)
    if m.rb(a2 + 0x36): return
    fm = friction_mode(m.rb(a2 + 0x58))
    if fm != -1: friction_25e7c(m, a2, fm)
    if pl and m.rw(0x400390) != 1: gravity_25df6(m, a2)


# 0x26196 / 0x261bc: speed cap
def speedcap_26196(m, a2):
    cap = 0x40000 if m.rb(a2 + 0x1a) == 0 else 0x50000
    d1 = abs(m.sl(a2)); d4 = abs(m.sl(a2 + 4))
    if d1 > d4: d3 = ((d4 >> 3) & 0xffff) * 3 + d1
    else: d3 = ((d1 >> 3) & 0xffff) * 3 + d4
    d3 &= 0xffffffff
    if cap < d3:
        q = ((cap << 6) // ((d3 >> 8) & 0xffff)) & 0xffff
        m.wl(a2, (s32(s16(m.sl(a2) >> 8) * s16(q)) >> 6) & 0xffffffff)
        m.wl(a2 + 4, (s32(s16(m.sl(a2 + 4) >> 8) * s16(q)) >> 6) & 0xffffffff)

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
# segments (tags 0x1a-0x1f) and cell rectangles that set the surface code +0x58.
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

def slots_29cce(m, a2):
    mcx = m.sw(0x400696); mcy = m.sw(0x400698); mx = m.sw(0x400690); my = m.sw(0x400692)
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
                    m.sounds.append(0x42)
            elif tag in RECTS:
                w, h, zc = RECTS[tag]
                if 0 <= d1 < w and 0 <= d2 < h and (not zc or m.rl(s + 0x14) == m.rl(a2 + 0x14)):
                    m.wb(a2 + 0x58, tag); m.wb(a2 + 0x59, 0xff)
            elif tag == 0x17:
                if d1 == 0 and -0x12 < d2 <= 0: m.wb(a2 + 0x58, tag); m.wb(a2 + 0x59, 0xff)
            elif tag == 0x18:
                if -0x12 < d1 <= 0 and d2 == 0: m.wb(a2 + 0x58, tag); m.wb(a2 + 0x59, 0xff)
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
    m.wb(a2 + 0x1a, 1)
    d4 = s8(m.rb(a2 + 0x57)) + fall
    if d4 > 0x7f: d4 = 0x7f
    m.wb(a2 + 0x57, d4)
    s56 = s8(m.rb(a2 + 0x56)); s57 = s8(m.rb(a2 + 0x57))
    if s56 + ((s57 - s56) >> 1) > 0x1f:
        m.events.append("broken")  # 0x25bae(a2, 2) or (4) off screen
        m.wb(a2 + 0x1a, 2)
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
                if m.rb(a2 + 0x1b) != d2: m.events.append(("zone", m.rb(a2 + 0x1b)))
                return
        if m.rb(a0 + 3) == 0xff and m.rb(a0 + 4) == 0xff: return
        a0 += 6

def tick(m, c6=None, c7=None, a2=P):
    """Snapshot point (0x121b8 entry) to the next one. c6/c7: trackball deltas of the next tick."""
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
    """Race start of one player on a cleared work RAM, up to the snapshot point of tick().
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
    # first object update before the first 0x121b8: 0x253bc, 0x17f66 (deltas are 0)
    ground_253bc(m, a2)
    input_17f66(m, a2)



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
    m = Mem(t, bytes(0x2000)); start_259b4(m, race)
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


def main(argv):
    if len(argv) == 3 and argv[1] == "fixture":
        return 0 if cmd_fixture(argv[2]) else 1
    if len(argv) < 4 or argv[1] not in ("terrain", "ticks", "run", "start"):
        print(__doc__); return 2
    trace = load_trace(argv[2]); race = int(argv[3])
    if argv[1] == "terrain": ok = cmd_terrain(trace, race)
    elif argv[1] == "ticks": ok = cmd_ticks(trace, race)
    elif argv[1] == "start": ok = cmd_start(trace, race)
    else: ok = cmd_run(trace, race, int(argv[4]) if len(argv) > 4 else 300)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
