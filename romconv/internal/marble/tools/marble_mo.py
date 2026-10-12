#!/usr/bin/env python3
"""Marble Madness motion object list (0x26f3e): level object slot sprites and their order.

Ports (Marble Love late-game-logic-26f3e.ts, mo-block-emit-1a8d2.ts, buffer-fill-1b12a.ts,
sort-adjacent-objects-1a7a8.ts; MARBLE.md "Level object sprites"):
  0x1b12a  depth box of a draw list entry (6 words: x, y, z min, x, y, z max)
  0x1a7a8  one pass of adjacent swaps with comparator 0x1a80a (3 passes, stride 1, 2, 3)
  0x26f3e  draw list 0x4003bc (bytes, 0xff end) -> MO words, link order = list order
  0x1a8d2  MO frame block (header + body) -> MO columns
Entity types follow Marble Love; types 1 (player), 3, 5, 6 (level object slots) are the ones
this tool checks, the rest (enemies 2, 4, 7-15, 0x29, 0x2a, 0x2c) are emitted only so the
list stays aligned with MO RAM.

usage:
  marble_mo.py check TRACE RACE      each snapshot: MO list from its work RAM == the MO RAM
                                     bank the game built last (0x4003b0); exit 1 on a
                                     difference in a slot type entity
  marble_mo.py sort TRACE RACE [F ...]  0x18e6c inserts, 0x18f46 removals, 0x1b12a + 0x1a7a8
                                     on the previous snapshot's list == this snapshot's list;
                                     F: MARBLE_PLACE frames to skip
  marble_mo.py render SHOTS SNAPDIR TRACE RACE OUTDIR [N ...]
                                     MAME PNG vs MO RAM render vs remake rule, pixel
                                     differences in a box around each level object
  marble_mo.py camcheck SHOTS SNAPDIR TRACE RACE [N ...]
                                     remake pipeline (published objects, marble, view scroll)
                                     vs MAME PNG on frames where the camera moves, per camera
                                     convention for the published y and the view scroll
  marble_mo.py data                  per race: drawn tags, frames, MO tiles, palette sets
  marble_mo.py spritefixture HEADER  replay the sprite host test fixture (make_fixture.py
                                     swrite): draw list, sort, published objects, no trace
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import marble_sim as ms
from marble_sim import rom_w, rom_l, s8, s16

LIST, LIST_END = 0x4003bc, 0x4003dc     # draw list bytes, 0xff end
RECTS = 0x1f0e2                         # long per list byte -> 14 byte entry (type, index, box)
ENTRIES, ENTRY_SIZE, NENTRIES = 0x4001dc, 14, 31    # entry k = ENTRIES + 14 k (RECTS[k])
LIST_MAX = 0x1f                         # list bytes before the sentinel limit (0x18e6c)
SLOT_TYPES = (3, 5, 6)                  # level object slot tags drawn by 0x26f3e (in scope)


class Ram:
    """Read access to a work RAM image (0x400000-0x401fff) and program ROM."""
    def __init__(self, ram): self.ram = bytearray(ram)
    def rb(self, a):
        a &= 0xffffffff
        if 0x400000 <= a < 0x402000: return self.ram[a - 0x400000]
        if a < 0x30000: return ms.PROG[a]
        return 0
    def rw(self, a): return self.rb(a) << 8 | self.rb(a + 1)
    def rl(self, a): return self.rw(a) << 16 | self.rw(a + 2)
    def wb(self, a, v): self.ram[a - 0x400000] = v & 0xff


# ------------------------------------------------------------------ 0x1a8d2 MO frame block

def frame_1a8d2(hdr, x, y, orm):
    """MO frame header at hdr (0xffffffff: nothing) at bias (x, y): list of (w0, code, w2).
    Header: s8 x bias, s8 y bias, 2 bytes, 4 box bytes (0x1b12a: dx, dy, sx, sy), long body
    (bit 0 = hflip, columns step -8 px). Body 0xff: short form, byte skipped, count, s8 dx,
    then count x (height - 1, s8 dy, word code); else long form: count, s8 dx, height - 1,
    s8 dy, count codes. One MO per column, 8 px apart, code | orm."""
    if hdr == 0xffffffff: return []
    x = (x + s8(ms.PROG[hdr])) & 0xffff; y = (y + s8(ms.PROG[hdr + 1])) & 0xffff
    b = rom_l(hdr + 8)
    flip, step = (0x8000, -8) if b & 1 else (0, 8)
    b &= ~1
    out = []
    if ms.PROG[b] == 0xff:
        n = ms.PROG[b + 2]; x = (x + s8(ms.PROG[b + 3])) & 0xffff; p = b + 4
        for k in range(n):
            h, dy, code = ms.PROG[p], s8(ms.PROG[p + 1]), rom_w(p + 2); p += 4
            out.append((((y + dy) << 5) & 0x3fe0 | flip | h, code | orm, ((x + step * k) << 5) & 0x3fe0))
    else:
        n, dx, h, dy = ms.PROG[b], s8(ms.PROG[b + 1]), ms.PROG[b + 2], s8(ms.PROG[b + 3])
        x = (x + dx) & 0xffff; w0 = ((y + dy) << 5) & 0x3fe0 | flip | h
        for k in range(n):
            out.append((w0, rom_w(b + 4 + 2 * k) | orm, ((x + step * k) << 5) & 0x3fe0))
    return out


# ------------------------------------------------------------------ 0x1b12a depth box

def box_1b12a(m, typ, idx):
    """Entry box words (x, y, z min, x, y, z max) per type (Marble Love buffer-fill-1b12a.ts)."""
    def obj(a, dx, dy, dz, sx, sy, sz):
        x, y, z = s16(m.rw(a + 0xc) + dx), s16(m.rw(a + 0x10) + dy), s16(m.rw(a + 0x14) + dz)
        return (x, y, z, s16(x + sx), s16(y + sy), s16(z + sz))
    def sub(p):
        return [s8(m.rb(p + k)) for k in (4, 5, 6, 7)]
    if typ == 0: return (0x7fff,) * 6
    if typ in (1, 2):
        return obj(m.rl((0x1eff6 if typ == 1 else 0x1effe) + 4 * idx), -3, -3, 1, 6, 6, 6)
    if typ in (4, 0xe):
        a2 = rom_l((0x1f006 if typ == 4 else 0x1f07a) + 4 * idx)
        p = m.rl(m.rl(a2 + (0x58 if typ == 4 else 0x3a)))
        dz, sz = (0, 0x10) if typ == 4 else (-8, 0)
        dx, dy, sx, sy = (-4, -4, 8, 8) if p == 0xffffffff else sub(p)
        return obj(a2, dx, dy, dz, sx, sy, sz)
    if typ == 0x29:
        a = 0x401650 + (idx << 4)
        x, y, z = s16((s8(m.rb(a + 4)) << 3) + 2), s16((s8(m.rb(a + 5)) << 3) + 2), s16(m.rw(a + 6))
        return (x, y, z, x + 4, y + 4, z + 0x14)
    if typ == 0x2a:
        a = 0x40098c + idx * 12
        z = s16(m.rw(a + 4) - 8)
        return (s16(m.rw(a)), s16(m.rw(a + 2)), z) * 2
    if typ == 0x2c: return (0,) * 6
    if typ in (7, 8, 9):
        a1 = rom_l(0x1f096 + 4 * idx)
        dx, dy, sx, sy = sub(m.rl(m.rl(a1 + 0x1c)))
        dz, sz = (-8, 0) if m.rb(a1 + 0x1a) == 2 else (0, 6)
        return obj(a1, dx, dy, dz, sx, sy, sz)
    if typ == 0xf:
        a1 = rom_l(0x1f0ba + 4 * idx)
        if m.rb(a1 + 0x1a) == 2: return obj(a1, 0, 0, 0, 0, 0, 0)
        return obj(a1, -4, -4, 0, 8, 8, 4)
    a1 = rom_l(0x1f016 + 4 * idx)                   # level object slot, tag = type
    if typ < 3 or typ > 0xd: return obj(a1, 0, 0, 0, 0, 0, 0)
    p = m.rl(m.rl(a1 + 0x3e))
    if typ == 3:
        dx, dy, sx, sy = sub(p); return obj(a1, dx, dy, -16, sx, sy, 0x10)
    if typ == 5:
        dx, dy, sx, sy = sub(p); return obj(a1, dx, dy, 0, sx, sy, 0x18)
    if typ == 6:
        return obj(a1, 0, s8(m.rb(p + 4)), 0, 0x20, s8(m.rb(p + 6)), 0x28)
    if typ == 10:
        dx, dy, sx, sy = sub(p); return obj(a1, dx, dy, -8, sx, sy, 0)
    if typ in (11, 12, 13):
        if p == 0xffffffff: return obj(a1, 0, 0, -8, 8, 8, 0)
        dx, dy, sx, sy = sub(p)
        return obj(a1, dx, dy, 0 if typ == 12 else -8, sx, sy, 0x10 if typ == 12 else 0x18)
    return obj(a1, 0, 0, 0, 0, 0, 0)


# ------------------------------------------------------------------ 0x1a7a8 / 0x1a80a sort

def entry(m, k):
    """List byte k -> 14 byte entry address (pointer table 0x1f0e2 is ENTRIES + 14 k)."""
    return ENTRIES + ENTRY_SIZE * k


def boxw(m, e):
    return [s16(m.rw(e + 2 + 2 * i)) for i in range(6)]


def cmp_boxes(p, q):
    """0x1a80a on box word lists (x, y, z min, x, y, z max). 1: p must be drawn after q."""
    if sum(p[3:]) <= sum(q[:3]): return 0
    if sum(q[3:]) <= sum(p[:3]): return 1
    if q[1] >= p[4]: return 0
    if p[1] >= q[4]: return 1
    if q[0] >= p[3]: return 0
    if p[0] >= q[3]: return 1
    if q[2] >= p[5]: return 0
    return 1


def cmp_1a80a(m, a1, a0):
    """1: entry a1 must be drawn after entry a0 (swap)."""
    return cmp_boxes(boxw(m, a1), boxw(m, a0))


def sort_1a7a8(m, stride):
    for i in range(0x20 - stride):
        a, b = LIST + i, LIST + i + stride
        if m.rb(a) == 0xff or m.rb(b) == 0xff: return
        if cmp_1a80a(m, entry(m, m.rb(a)), entry(m, m.rb(b))):
            va, vb = m.rb(a), m.rb(b); m.wb(a, vb); m.wb(b, va)


def draw_list(m):
    out = []
    a = LIST
    while m.rb(a) != 0xff and a < LIST_END - 1:
        out.append(m.rb(a)); a += 1
    return out


def boxes_and_sort(m):
    """0x26f3e phases 1-2: refresh every entry box (0x1b12a), 3 sort passes (0x4003e2 == 0)."""
    for k in draw_list(m):
        e = entry(m, k)
        for i, v in enumerate(box_1b12a(m, m.rb(e), m.rb(e + 1))):
            m.wb(e + 2 + 2 * i, (v & 0xffff) >> 8); m.wb(e + 3 + 2 * i, v & 0xff)
    if m.rb(0x4003e2) == 0:
        for s in (1, 2, 3): sort_1a7a8(m, s)


# ------------------------------------------------------------------ 0x26f3e MO list

def pos(m, a, off, xb=0x18, yb=0x10):
    return (m.rw(a + off) + xb) & 0xffff, (m.rw(a + off + 2) + yb) & 0xffff


def mo(x, y, code, w0bits=0, w2bits=0):
    return ((y << 5) & 0x3fe0 | w0bits, code & 0xffff, (x << 5) & 0x3fe0 | w2bits)


def entries6(m, a, n, xflag=None, setbits=0):
    """Inline 6 byte MO entries (code word, x, y words; first word 0 ends)."""
    out = []
    for i in range(n):
        w0 = m.rw(a + 6 * i)
        if not w0: break
        d6 = (w0 & 0x8000) | ((w0 >> 11) & 7)
        x, y = s16(m.rw(a + 6 * i + 2)), s16(m.rw(a + 6 * i + 4))
        if xflag == "mask":
            code = w0 & 0x7ff if w0 & 0x4000 else (w0 & 0x7ff) | setbits
            xf = 0x8000 if w0 & 0x4000 else 0
        else:
            code = (w0 & 0x7ff) | setbits; xf = xflag or 0
        out.append((((y + 0x10) << 5) & 0x3fe0 | d6, code & 0xffff, ((x + 0x18) << 5) & 0x3fe0 | xf))
    return out


def type1(m, idx):
    """Player marble (0x2724c): halves 0x05/0x03, surface dots (+0xa4), 0x07, masks (+0x38)."""
    a = rom_l(0x1eff6 + 4 * idx)
    if not m.rb(a + 0x1c): return []
    st = m.rb(a + 0x1a); f10 = (m.rb(a + 0x19) << 11) & 0xffff
    ext = lambda w: (0xffff0000 | w) if w & 0x8000 else w
    d5, d4 = pos(m, a, 0x1e)
    out = []
    a1w, d2, le, fn1 = d5, d4, 5, 0
    if st not in (4, 10, 11, 9, 2, 1, 5):
        if st == 8:
            v = m.rl(m.rl(a + 0xcc))
            if v == 0x215c6: le, d2, fn1 = 0x104, (d2 - 5) & 0xffff, 1
            elif v == 0x215ea: le, a1w, fn1 = 0x108, (a1w + 2) & 0xffff, 2
        le |= ext(f10)
        out.append(mo(s16(a1w) - 8, s16(d2), le & 0xffff, 1))
        le = (le + 2) if st == 8 and fn1 else (ext(f10) | 3)
        out.append(mo(s16(a1w), s16(d2), le & 0xffff, 1))
    if st in (2, 9, 1, 5):
        out += frame_1a8d2(m.rl(m.rl(a + 0x5a)), d5, d4, f10)
    else:
        out += entries6(m, a + 0xa4, 4, 0, f10)
        if st not in (4, 10, 11) and not (st == 8 and fn1):
            e3 = ext(f10) | 7
            out.append(mo(s16(d5) - 8, s16(d4) + 5, e3 & 0xffff, (e3 >> 16) & 0xffff))
    out += entries6(m, a + 0x38, 5, "mask", f10)
    if m.rb(a + 0x67): out += frame_1a8d2(m.rl(m.rl(a + 0x62)), d5, d4, 0x1000)
    if m.rb(a + 0xd1): out += frame_1a8d2(m.rl(m.rl(a + 0xcc)), d5, d4, 0x1000)
    if m.rb(a + 0xd8) and st not in (4, 10, 11, 7, 2):
        out += frame_1a8d2(m.rl(a + 0xd4), (d5 + s8(m.rb(a + 0x68))) & 0xffff,
                           (d4 + (0x20 if st == 6 else 0x10)) & 0xffff, f10)
    return out


def slot_mos(m, typ, idx):
    """Level object slot (tag = type, 0x1f016[index]): screen position +0x4e, MO frame +0x42."""
    a = rom_l(0x1f016 + 4 * idx)
    d5, d4 = pos(m, a, 0x4e, 0x17 if typ == 5 else 0x18)
    y = s16(d4)
    if typ == 3:                                    # 0x27a16: three global parts + own frame
        if y <= -0x40 or y >= 0x160: return []      # ROM 0x27a44 (Marble Love lacks -0x40)
        out = []
        for g in (0x40044a, 0x40044e, 0x400452):
            out += frame_1a8d2(m.rl(m.rl(g)), d5, d4, 0x3800)
        return out + frame_1a8d2(m.rl(m.rl(a + 0x42)), d5, d4, 0x3000)
    if typ == 5:                                    # 0x27dd0
        if y <= -0x40 or y >= 0x100: return []
        return frame_1a8d2(m.rl(m.rl(a + 0x42)), d5, d4, 0x1800)
    if typ == 6:                                    # 0x27ed4 wave
        p42, p46 = m.rl(a + 0x42), m.rl(a + 0x46)
        if y <= -0x40 or y >= 0x140: return []
        hdr = m.rl(p42)                             # ROM 0x27f22: set 7 from frame 0x220ee on
        out = frame_1a8d2(hdr, d5, d4, 0x3800 if hdr >= 0x220ee else 0x2800)
        if p46 != 0x2227a: return out
        i = s16(((p42 - p46) >> 2) & 0xffff)
        if i < 0 or rom_w(0x20f64 + 2 * i) == 0xffff: return out
        tw = rom_w(0x20f64 + 2 * i); yo = s8(ms.PROG[0x20f92 + i])
        x, y2 = (d5 + 8) & 0xffff, (d4 - 0x40 + yo) & 0xffff
        for _ in range(4):
            out.append(mo(s16(x), s16(y2), tw & 0x7ff, (tw >> 11) & 7, 0x8000))   # 0x27fa2
            x = (x + 8) & 0xffff; y2 = (y2 + 4) & 0xffff
        return out
    if typ == 10:
        if y <= -0x40 or y >= 0x120: return []
        return frame_1a8d2(m.rl(m.rl(a + 0x42)), d5, d4, 0x3000)
    if typ in (11, 13):
        if y <= -0x20 or y >= 0x100: return []
        out = frame_1a8d2(m.rl(m.rl(a + 0x42)), d5, d4, 0x1800)
        t = m.rb(a + 0x1f); x, y2 = (d5 - 8) & 0xffff, (d4 - 0x40 + (4 if t == 0xd else 0)) & 0xffff
        for k in range(2):
            out.append(mo(s16(x), s16(y2), 0x500, 6 | (0x8000 if t == 0xd else 0), 0x8000))
            y2 = (y2 + (-4 if t == 0xd else 4)) & 0xffff; x = (x + 8) & 0xffff
        return out
    if typ == 12:
        if y <= -0x20 or y >= 0x100: return []
        return frame_1a8d2(m.rl(m.rl(a + 0x42)), d5, d4, 0x3800)
    return []


def other_mos(m, typ, idx):
    """Enemies and the rest (Marble Love), only to keep the list aligned."""
    if typ == 2:
        a = rom_l(0x1effe + 4 * idx)
        if not m.rb(a + 0x1c): return []
        d5, d4 = pos(m, a, 0x1e)
        hdr = 0x21f36 if m.rb(a + 0x18) != 2 else m.rl(m.rl(a + 0x5a))
        out = frame_1a8d2(hdr, d5, d4, 0x2800) + entries6(m, a + 0x38, 5, 0x8000)
        if m.rb(a + 0x67): out += frame_1a8d2(m.rl(m.rl(a + 0x62)), d5, d4, 0x1000)
        return out
    if typ == 4:
        a = rom_l(0x1f006 + 4 * idx)
        d5, d4 = pos(m, a, 0x28)
        if s16(d4) <= -0x20 or s16(d4) >= 0x100: return []
        return frame_1a8d2(m.rl(m.rl(a + 0x58)), d5, d4, 0x2000) + entries6(m, a + 0x2c, 5, 0x8000)
    if typ in (7, 8, 9):
        a = rom_l(0x1f096 + 4 * idx)
        d5, d4 = pos(m, a, 0x20)
        if s16(d4) <= -0x10 or s16(d4) >= 0x100: return []
        return frame_1a8d2(m.rl(m.rl(a + 0x1c)), d5, d4, {7: 0x2800, 8: 0x3000, 9: 0x2000}[typ])
    if typ == 14:
        a = rom_l(0x1f07a + 4 * idx)
        d5, d4 = pos(m, a, 0x28)
        if s16(d4) <= -0x30 or s16(d4) >= 0x120: return []
        return frame_1a8d2(m.rl(m.rl(a + 0x3a)), d5, d4, 0x3000)
    if typ == 15:
        a = rom_l(0x1f0ba + 4 * idx)
        d5, d4 = pos(m, a, 0x20)
        if s16(d4) <= -0x10 or s16(d4) >= 0x100: return []
        return frame_1a8d2(m.rl(m.rl(a + 0x1c)), d5, d4, 0x3800) + entries6(m, a + 0x26, 3, 0x8000)
    if typ == 0x29:
        a = 0x401650 + (idx << 4)
        d5, d4 = pos(m, a, 0xc)
        if s16(d4) <= -0x40 or s16(d4) >= 0x100: return []
        return frame_1a8d2(m.rl(m.rl(a + 8)), d5, d4, 0x2000)
    if typ == 0x2a:
        a = 0x40098c + s8(idx) * 12
        d5, d4 = pos(m, a, 6)
        if s16(d4) <= -0x40 or s16(d4) >= 0x100: return []
        d2 = (s8(idx) << 11) & 0xffff
        c1, dx = (0x102, -1) if m.rb(a + 0xa) else (0x100, -0xc)
        x, y = s16(d5) + dx, s16(d4) - 5
        return [mo(x, y, 0), mo(x + 8, y, (c1 | d2) + 1)]
    if typ == 0x2c:
        a = 0x400a9c + s8(idx) * 10
        d5 = s16(m.rw(a)) >> 4; d4 = (s16(m.rw(a + 2)) >> 4) + 0x10
        la = s16(m.rw(a + 8))
        return [mo(d5 - 8, d4, (la | 0x10001) & 0xffff, 1), mo(d5, d4, (la | 0x10003) & 0xffff, 1)]
    return []


def mo_list_26f3e(m, sort=False):
    """[(list byte, type, index, [(w0, code, w2), ...])] in link order. sort: run the box
    refresh and the sort passes first (the snapshot's list is already sorted by the last
    0x26f3e)."""
    if sort: boxes_and_sort(m)
    out = []; n = 0
    for k in draw_list(m):
        if n >= 0x3c: break
        e = entry(m, k); typ, idx = m.rb(e), m.rb(e + 1)
        if typ == 1: mos = type1(m, idx)
        elif typ in (3, 5, 6, 10, 11, 12, 13): mos = slot_mos(m, typ, idx)
        else: mos = other_mos(m, typ, idx)
        out.append((k, typ, idx, mos)); n += len(mos)
    return out


def mo_ram(ram, vram):
    """MO words of the bank 0x26f3e built last (0x4003b0 bit 3), in RAM order, count 0x400406."""
    bank = 0x200 if ram[0x3b1] & 8 else 0
    n = struct.unpack(">H", ram[0x406:0x408])[0]
    w = lambda k, i: struct.unpack(">H", vram[0x2000 + bank + 0x80 * k + 2 * i:0x2002 + bank + 0x80 * k + 2 * i])[0]
    return [(w(0, i), w(1, i), w(2, i)) for i in range(n)], [w(3, i) for i in range(n)]


# ------------------------------------------------------------------ commands

def cmd_check(trace, race):
    """Per snapshot: entities in list order, emitted MOs == MO RAM. Slot types must match."""
    n = slots_seen = slot_bad = other_bad = ticks_with = lag = 0
    prev = None
    for fr, ram, vram in trace:
        gap, prev = prev is not None and fr - prev != 2, fr
        if ram[0x3bc] == 0xff or ms.tw(ram, 0x400390) == 1: continue
        m = Ram(ram)
        lst = mo_list_26f3e(m)
        ref, links = mo_ram(ram, vram)
        got = [w for _, _, _, mos in lst for w in mos]
        n += 1
        pos_ = 0; tick_slot = False
        for k, typ, idx, mos in lst:
            seg = ref[pos_:pos_ + len(mos)]
            if typ in SLOT_TYPES and mos:
                slots_seen += 1; tick_slot = True
                if seg != mos and gap:
                    lag += 1                        # logic tick took 3+ frames
                elif seg != mos:
                    slot_bad += 1
                    if slot_bad <= 5:
                        print("frame %d type %d slot %d: got %s\n  ref %s" % (
                            fr, typ, idx, [tuple(hex(v) for v in w) for w in mos],
                            [tuple(hex(v) for v in w) for w in seg]))
            elif seg != mos:
                other_bad += 1
            pos_ += len(mos)
        if len(got) != len(ref): other_bad += 1
        bad_link = [i for i, l in enumerate(links) if l != (i + 1 if i + 1 < len(links) else i)]
        if bad_link: other_bad += 1
        ticks_with += tick_slot
    print("check race %d: %d snapshots, %d with slot MOs, %d slot entities drawn, %d differ; "
          "%d differ after a lag frame; other types/count/link differences %d"
          % (race, n, ticks_with, slots_seen, slot_bad, lag, other_bad))
    return slot_bad == 0


def insert_18e6c(m, lst, k):
    """0x18e6c: new entry k goes before the first entry e with 0x1a80a(e, k) != 0, else last."""
    for i, e in enumerate(lst):
        if cmp_1a80a(m, entry(m, e), entry(m, k)): return lst[:i] + [k] + lst[i:]
    return lst + [k]


def new_box(m, m0, k):
    """Entry k as 0x18e6c sees it in m0 (the boxes of the last 0x26f3e): its own box from the
    later RAM m, with the slot animation still empty (op 0 sets +0x3e = 0x20c14 first)."""
    e = entry(m, k); typ, idx = m.rb(e), m.rb(e + 1)
    t = Ram(m.ram)
    if typ not in (1, 2, 4, 7, 8, 9, 0xe, 0xf, 0x29, 0x2a, 0x2c):
        a = rom_l(0x1f016 + 4 * idx) - 0x400000
        t.ram[a + 0x3e:a + 0x42] = struct.pack(">I", ms.ANIM_NONE)
    m0.wb(e, typ); m0.wb(e + 1, idx)
    for i, v in enumerate(box_1b12a(t, typ, idx)):
        m0.wb(e + 2 + 2 * i, (v & 0xffff) >> 8); m0.wb(e + 3 + 2 * i, v & 0xff)
    return k


def set_list(m, lst):
    for i, k in enumerate(lst + [0xff]): m.wb(LIST + i, k)


def cmd_sort(trace, race, skip=()):
    """Snapshot pairs: previous list (+ 0x18e6c inserts of new entries, 0x18f46 removals, boxes
    of the later snapshot) -> box refresh + 3 sort passes == later list."""
    n = same = moved = ins = rem = 0
    for (fr0, r0, _), (fr, r, _) in zip(trace, trace[1:]):
        m = Ram(r)
        if any(fr0 <= f <= fr for f in skip): continue    # MARBLE_PLACE teleport between
        m0 = Ram(r0)
        prev, cur = draw_list(m0), draw_list(m)
        if not cur or len(prev) >= 0x1f: continue          # list not set up yet
        new = [k for k in cur if k not in prev]
        gone = [k for k in prev if k not in cur]
        if len(new) + len(gone) > 1: continue
        lst = [k for k in prev if k not in gone]
        for k in new: lst = insert_18e6c(m0, lst, new_box(m, m0, k))
        ins += len(new); rem += len(gone)
        set_list(m, lst)
        boxes_and_sort(m)
        n += 1; moved += prev != cur
        if draw_list(m) == cur: same += 1
        else:
            print("frame %d: prev %s new %s gone %s -> %s, ref %s" % (fr, prev, new, gone, draw_list(m), cur))
    print("sort race %d: %d snapshot pairs (%d with a new list order, %d inserts, %d removals), %d equal"
          % (race, n, moved, ins, rem, same))
    return n == same


# ------------------------------------------------------------------ render check

PAL_CMDS = 0x20ae4      # palette command -> descriptor (count, dest offset, words)
PAL_DESTS = 0x20840     # palette RAM address per descriptor word
TAG3_PAL = 0x1df18      # tag 3 frame index (+7 for list 0x21192) -> palette command


def tag3_palette(m, a):
    """Palette words the tag 3 animation queues for its current frame (0x13334 -> 0x26b66 ->
    0x26b88): {palette index: word}."""
    i = ((m.rl(a + 0x3e) - m.rl(a + 0x46)) >> 3) & 0xffff
    if m.rl(a + 0x46) == 0x21192: i += 7
    d = rom_l(PAL_CMDS + 4 * ms.PROG[TAG3_PAL + i])
    return {(rom_l(PAL_DESTS + ms.PROG[d + 1] + 4 * k) - 0xb00000) // 2: rom_w(d + 2 + 2 * k)
            for k in range(ms.PROG[d])}


def entity_words(trace, fr, ram, vram, bank):
    """Model MO list whose words equal the shown bank: from the shot RAM or a trace snapshot of
    the last 6 frames (the shot is taken at frame end, mid logic tick)."""
    n = len(bank)
    cands = [ram] + [r for f, r, _ in reversed(trace) if fr - 6 <= f <= fr]
    for r in cands:
        lst = mo_list_26f3e(Ram(r))
        if [w for *_, mos in lst for w in mos][:n] == bank and sum(len(x[3]) for x in lst) == n:
            return lst, r
    return None, None


def mo_box(mos):
    """Screen box (x0, y0, x1, y1) of MO words (MAME atarimo: top = -y - 256 - 8 h)."""
    xs, ys = [], []
    for w0, _, w2 in mos:
        h = (w0 & 15) + 1; x = (w2 >> 5) & 0x1ff
        top = (-((w0 >> 5) & 0x1ff) - 256 - h * 8) & 0x1ff
        if top >= 0x100: top -= 0x200
        xs += [x, x + 8]; ys += [top, top + 8 * h]
    return min(xs), min(ys), max(xs), max(ys)


def cmd_render(argv):
    """MAME PNG vs MO RAM render (MAME merge) vs remake rule A (masks clear the marble only) and
    B (masks clear every MO drawn before them, as MAME), in a box around each slot object."""
    import marble_data as md
    import marble_occlusion as mocc
    from PIL import Image
    shots, snapdir, trace, race, outdir = argv[2], argv[3], ms.load_trace(argv[4]), int(argv[5]), argv[6]
    os.makedirs(outdir, exist_ok=True)
    rom = md.load()
    recs = ms.load_trace(shots)
    palf = os.path.join(os.path.dirname(shots), "shots_pal.bin")
    pals = open(palf, "rb").read() if os.path.exists(palf) else None
    pick = [int(a) for a in argv[7:]] or range(len(recs))
    tot = {"shots": 0, "objects": 0, "mo": 0, "A": 0, "B": 0, "masked": 0, "unmatched": 0,
           "pal": 0, "palpred": 0, "behind": 0}
    yoff_seen = None
    for n in pick:
        fr, ram, vram = recs[n]
        if ram[0x3bc] == 0xff: continue
        f = mocc.Frame(rom, race, ram, vram)
        pram = pals[n * 0x800:(n + 1) * 0x800] if pals else None
        if pram:
            f.pal = [md._irgb(struct.unpack(">H", pram[2 * i:2 * i + 2])[0]) for i in range(1024)]
        snap = Image.open(os.path.join(snapdir, "%04d.png" % n)).convert("RGB")
        best = None                                 # playfield line offset, calibrated once
        for yoff in ([yoff_seen] if yoff_seen is not None else []) + list(range(-16, 17)):
            pf = f.playfield(yoff)
            d = mocc.diff(f.merge(pf, [[None] * mocc.W for _ in range(mocc.H)]), snap)
            if best is None or d < best[0]: best = (d, yoff, pf)
            if yoff == yoff_seen and d < 8000: break
        yoff_seen, pf = best[1], best[2]
        renders = {}
        for b in (0, 1):
            layer = [[None] * mocc.W for _ in range(mocc.H)]
            f.draw_mos(f.mo_list(b), layer)
            renders[b] = f.merge(pf, layer)
        b = min((0, 1), key=lambda k: mocc.diff(renders[k], snap))
        shown = f.mo_list(b)
        lst, src = entity_words(trace, fr, ram, vram, shown)
        if lst is None:
            tot["unmatched"] += 1; continue
        slots = [(typ, idx, mos) for _, typ, idx, mos in lst if typ in SLOT_TYPES and mos]
        if not slots: continue
        tot["shots"] += 1

        # remake rule: link order, masks (player entries with bit 14) clear A: marble only, B: all
        imgs = {}
        for rule in ("A", "B"):
            layer = [[None] * mocc.W for _ in range(mocc.H)]
            for _, typ, idx, mos in lst:
                if typ == 1:
                    body = [w for w in mos if not w[2] & 0x8000]
                    f.draw_mos(body, layer, "marble")
                    masks = [w for w in mos if w[2] & 0x8000]
                    probe = [[None] * mocc.W for _ in range(mocc.H)]
                    f.draw_mos(masks, probe)
                    for y in range(mocc.H):
                        for x in range(mocc.W):
                            if probe[y][x] is None or layer[y][x] is None: continue
                            tag = layer[y][x][3]
                            if rule == "A" and tag != "marble": continue
                            tot["masked"] += rule == "B" and tag == "slot"
                            layer[y][x] = None
                elif typ in SLOT_TYPES:
                    f.draw_mos(mos, layer, "slot")
                else:
                    f.draw_mos(mos, layer, "other")
            imgs[rule] = f.merge(pf, layer)

        # palette: tag 3 queued words vs palette RAM of the shot
        m = Ram(src)
        for typ, idx, _ in slots:
            if typ != 3 or not pram: continue
            for i, wv in tag3_palette(m, rom_l(0x1f016 + 4 * idx)).items():
                tot["palpred"] += 1
                tot["pal"] += struct.unpack(">H", pram[2 * i:2 * i + 2])[0] != wv

        pl = [k for k, (_, typ, _, _) in enumerate(lst) if typ == 1]
        for typ, idx, mos in slots:
            x0, y0, x1, y1 = mo_box(mos)
            box = (x0 - 8, max(24, y0 - 8), min(288, x1 + 8), y1 + 8)
            k = next(i for i, e in enumerate(lst) if e[1] == typ and e[2] == idx)
            tot["behind"] += bool(pl) and k < pl[0]
            d = [mocc.diff(im, snap, box) for im in (renders[b], imgs["A"], imgs["B"])]
            tot["objects"] += 1
            tot["mo"] += d[0] > 0; tot["A"] += d[1] > 0; tot["B"] += d[2] > 0
            print("shot %d frame %d type %d slot %d box %s: MO RAM %d px, rule B %d px, rule A %d px"
                  % (n, fr, typ, idx, box, d[0], d[2], d[1]))
        out = Image.new("RGB", (4 * mocc.W + 30, mocc.H), (40, 40, 40))
        for k, im in enumerate((snap, renders[b], imgs["B"], imgs["A"])):
            out.paste(im, (k * (mocc.W + 10), 0))
        out.resize((out.width * 2, out.height * 2), Image.NEAREST).save(
            os.path.join(outdir, "mo_%04d.png" % n))
    print("render race %d: %d shots with slot objects (%d unmatched), %d object boxes; boxes "
          "differing from MAME: MO RAM render %d, rule B %d, rule A %d; slot boxes before the "
          "player in link order %d; slot pixels a mask cleared %d; tag 3 palette words %d, "
          "%d differ from palette RAM" % (race, tot["shots"], tot["unmatched"], tot["objects"],
                                          tot["mo"], tot["B"], tot["A"], tot["behind"],
                                          tot["masked"], tot["palpred"], tot["pal"]))
    return tot["mo"] == 0 and tot["B"] == 0


# ------------------------------------------------------------------ camera convention check

def slot_camera(m, a):
    """Camera word a level object slot's screen position +0x50 was computed with (0x13334):
    sy = camera + z + 0x54 - ((x + y) >> 1) on the slot's own position words."""
    sy = m.rw(a + 0x50)
    xy = (s16(m.rw(a + 0xc)) + s16(m.rw(a + 0x10))) >> 1
    return (sy - m.rw(a + 0x14) - 0x54 + xy) & 0xffff


def marble_camera(m):
    """Same for the player projection 0x1b9cc (+0x20, position copy 0x400690/2); None on the
    bumpy surfaces (2 px jitter term)."""
    if m.rb(P_ + 0x58) in (0x2f, 0x30, 0x31): return None
    xy = (s16(m.rw(0x400690)) + s16(m.rw(0x400692))) >> 1
    return (m.rw(P_ + 0x20) - m.rw(P_ + 0x14) - 0x54 + xy) & 0xffff


P_ = ms.P


def published_mos(sp, objs, top):
    """Published objects -> MAME MO words (w0, code, w2) with the view top line subtracted: the
    remake's screen position of every column."""
    out = []
    for fid, st, ex, ey in objs:
        for x, y, hh, tile in sp.objs_cols(fid):
            h = hh & 0x7f
            line = (ey + y - top) & 0x1ff
            code = sp.tile_codes[tile] | (0 if st == SET_SHADE else st << 11)
            out.append(((-(line + 8 * h + 256) & 0x1ff) << 5 | (hh >> 7) << 15 | (h - 1),
                        code, ((ex + x + 24) & 0x1ff) << 5 | (0x8000 if st == SET_SHADE else 0)))
    return out


def shift_mos(mos, d):
    """MO words with the screen line moved d lines down."""
    return [((w0 & ~0x3fe0) | ((((w0 >> 5) & 0x1ff) - d) & 0x1ff) << 5, c, w2) for w0, c, w2 in mos]


def cmd_camcheck(argv):
    """Remake pipeline (published objects and marble in race pixels, playfield at the view
    scroll) vs the MAME PNG on frames where the camera moves. The C++ takes the race y of the
    objects and the marble ('pub') and the view scroll ('view') from one camera word, the one
    the slot update and the projection used. Rows: (pub, view) camera pairs; 'update' the
    camera the slot update used (derived from the stored screen position), 'word' the camera
    word of the matched state, 'mame' the playfield scroll MAME shows (reference render)."""
    import marble_data as md
    import marble_occlusion as mocc
    from PIL import Image
    shots, snapdir, trace, race = argv[2], argv[3], ms.load_trace(argv[4]), int(argv[5])
    rom = md.load()
    recs = ms.load_trace(shots)
    palf = os.path.join(os.path.dirname(shots), "shots_pal.bin")
    pals = open(palf, "rb").read() if os.path.exists(palf) else None
    terrain, sp = ms.Terrain(race), Sprites(race)
    pick = [int(a) for a in argv[6:]]
    if not pick:                                    # shots where the camera moves around the frame
        for n, (fr, ram, vram) in enumerate(recs):
            cams = {ms.tw(r, 0x40097e) for f, r, _ in trace if fr - 4 <= f <= fr + 4}
            if len(cams) > 1 and ram[0x3bc] != 0xff: pick.append(n)

    # pass 1: MAME frame, playfield scroll calibration, matched state
    shotsd, yoff_seen = [], None
    for n in pick:
        fr, ram, vram = recs[n]
        f = mocc.Frame(rom, race, ram, vram)
        if pals:
            pram = pals[n * 0x800:(n + 1) * 0x800]
            f.pal = [md._irgb(struct.unpack(">H", pram[2 * i:2 * i + 2])[0]) for i in range(1024)]
        snap = Image.open(os.path.join(snapdir, "%04d.png" % n)).convert("RGB")
        best = None
        for yoff in ([yoff_seen] if yoff_seen is not None else []) + list(range(-16, 17)):
            d = mocc.diff(f.merge(f.playfield(yoff), [[None] * mocc.W for _ in range(mocc.H)]), snap)
            if best is None or d < best[0]: best = (d, yoff)
            if yoff == yoff_seen and d < 8000: break
        yoff_seen = best[1]
        renders = {}
        for b in (0, 1):
            layer = [[None] * mocc.W for _ in range(mocc.H)]
            f.draw_mos(f.mo_list(b), layer)
            renders[b] = f.merge(f.playfield(best[1]), layer)
        b = min((0, 1), key=lambda k: mocc.diff(renders[k], snap))
        lst, src = entity_words(trace, fr, ram, vram, f.mo_list(b))
        if lst is not None: shotsd.append((n, fr, ram, f, snap, best[1], lst, src, renders[b]))
    yoffs = {}
    for e in shotsd: yoffs[e[5]] = yoffs.get(e[5], 0) + 1

    convs = (("pub update, view update", "u", "u"), ("pub update, view word", "u", "w"),
             ("pub word, view word", "w", "w"), ("pub update, view MAME", "u", "m"))
    tot = {c[0]: {"bad": 0, "px": 0} for c in convs}
    tot["MAME MO RAM render"] = {"bad": 0, "px": 0}
    cam_rel, mar_rel, ysd = {}, {}, {}
    for n, fr, ram, f, snap, yoff, lst, src, mo_img in shotsd:
        m = ms.Mem(terrain, src)
        dt = m.sw(m.t.desc + 0x10)
        ys = struct.unpack(">H", ram[0:2])[0]
        cam_shot = struct.unpack(">H", ram[0x97e:0x980])[0]
        ysd[(ys - (cam_shot - dt) + 0x200) % 0x200] = ysd.get((ys - (cam_shot - dt) + 0x200) % 0x200, 0) + 1
        cam_word = m.rw(0x40097e)
        cams = {slot_camera(m, rom_l(0x1f016 + 4 * idx)) for _, typ, idx, mos in lst
                if typ in SLOT_TYPES and mos for idx in (idx,)}
        if len(cams) > 1:
            print("shot %d: slots used different cameras %s" % (n, sorted(cams)))
            continue
        cam_upd = cams.pop() if cams else cam_word
        cam_rel[s16(cam_upd - cam_word)] = cam_rel.get(s16(cam_upd - cam_word), 0) + 1
        top_mame = s16(cam_shot) - dt + yoff        # race row of the playfield line MAME shows
        pl_entity = next((mos for _, typ, _, mos in lst if typ == 1), [])
        sy_m = m.sw(P_ + 0x20)
        boxes = [(x0 - 8, max(24, y0 - 8), min(288, x1 + 8), y1 + 8) for x0, y0, x1, y1 in
                 (mo_box(mos) for _, typ, _, mos in lst if (typ in SLOT_TYPES or typ == 1) and mos)]
        if pl_entity:
            w0 = pl_entity[0][0]
            l05 = (-((w0 >> 5) & 0x1ff) - 256 - 8 * ((w0 & 15) + 1)) & 0x1ff
            mar_rel[l05 - (224 - sy_m)] = mar_rel.get(l05 - (224 - sy_m), 0) + 1
        d = sum(mocc.diff(mo_img, snap, bx) for bx in boxes)
        tot["MAME MO RAM render"]["bad"] += d > 0
        tot["MAME MO RAM render"]["px"] += d
        for name, kp, kv in convs:
            cam_pub = cam_upd if kp == "u" else cam_word
            top_pub = s16(cam_pub) - dt
            top_view = {"u": s16(cam_upd) - dt, "w": s16(cam_word) - dt, "m": top_mame}[kv]
            layer = [[None] * mocc.W for _ in range(mocc.H)]
            for _, typ, idx, emos in lst:                   # link order, as published
                if typ in SLOT_TYPES:
                    objs, _ = slot_objs(m, sp, typ, idx, top_pub, 0)
                    f.draw_mos(published_mos(sp, objs, top_view), layer, "slot")
                elif typ != 1 or not emos:
                    f.draw_mos(emos, layer, "other")        # enemies: not ported, as MAME
                else:
                    l05 = (-((emos[0][0] >> 5) & 0x1ff) - 256 - 8 * ((emos[0][0] & 15) + 1)) & 0x1ff
                    pl = shift_mos(emos, (top_pub + 224 - sy_m - top_view) - l05)
                    f.draw_mos([w for w in pl if not w[2] & 0x8000], layer, "marble")
                    probe = [[None] * mocc.W for _ in range(mocc.H)]
                    f.draw_mos([w for w in pl if w[2] & 0x8000], probe)
                    for y in range(mocc.H):
                        for x in range(mocc.W):
                            if probe[y][x] is not None and layer[y][x] is not None and \
                                    layer[y][x][3] == "marble":
                                layer[y][x] = None
            # playfield line of screen line 0 = race row of the view top (scroll = camera - top)
            img = f.merge(f.playfield(top_view - ys), layer)
            d = sum(mocc.diff(img, snap, bx) for bx in boxes)
            if d and name == convs[0][0] and os.environ.get("CAMCHECK_V"):
                print("  shot %d frame %d: %d px, boxes %s, entities %s" % (
                    n, fr, d, [mocc.diff(img, snap, bx) for bx in boxes],
                    [(typ, idx) for _, typ, idx, mos in lst if (typ in SLOT_TYPES or typ == 1) and mos]))
            tot[name]["bad"] += d > 0
            tot[name]["px"] += d
    print("camcheck race %d: %d shots with a moving camera, %d matched a state" % (race, len(pick), len(shotsd)))
    print("  MAME playfield line - work RAM scroll at the shot: %s" % sorted(yoffs.items()))
    print("  work RAM scroll - (camera word - descriptor top), mod 512: %s" % sorted(ysd.items()))
    print("  slot update camera - camera word of the matched state: %s" % sorted(cam_rel.items()))
    print("  marble MO 0x05 screen line - (224 - sy): %s" % sorted(mar_rel.items()))
    for name in [c[0] for c in convs] + ["MAME MO RAM render"]:
        print("  %-26s %d of %d shots differ from MAME, %d px in the object and marble boxes"
              % (name, tot[name]["bad"], len(shotsd), tot[name]["px"]))
    return all(v["bad"] == 0 for k, v in tot.items() if k.endswith("update"))


TAG3_GLOBALS = (0x211fe, 0x2121e, 0x2126e, 0x212b2)   # 0x40044a/e/52 lists (set 7)
WAVE_STRIP = (0x20f64, 0x20f92, 23)                    # tag 6 list 0x2227a: words, s8 dy


def frame_tiles(hdr):
    """Header -> (header + body bytes, MO tile codes (code & 0x7ff, every tile of a column))."""
    b = rom_l(hdr + 8) & ~1
    body = 4 + 4 * ms.PROG[b + 2] if ms.PROG[b] == 0xff else 4 + 2 * ms.PROG[b]
    tiles = set()
    for w0, code, _ in frame_1a8d2(hdr, 0, 0, 0):
        tiles.update((code + t) & 0x7ff for t in range((w0 & 15) + 1))
    return 12 + body, tiles


def race_frames(race):
    """Drawn slot tags of one race -> {(list, orm): [headers]} (mode 0 lists hold headers,
    mode 2 pairs (header, terrain block))."""
    import objects_data as od
    _, roots = od.scripts_of(race)
    _, anims, _ = od.walk(roots)
    out = {}
    for lst, stride, tag, mode in sorted(anims):
        if tag not in SLOT_TYPES or mode == 1: continue
        hs = []
        while rom_l(lst + stride * len(hs)) != 0xffffffff: hs.append(rom_l(lst + stride * len(hs)))
        orm = {3: 0x3000, 5: 0x1800, 6: None}[tag]
        out[(tag, lst, orm)] = hs
        if tag == 3:
            for g in TAG3_GLOBALS:
                gs = []
                while rom_l(g + 4 * len(gs)) != 0xffffffff: gs.append(rom_l(g + 4 * len(gs)))
                out[(3, g, 0x3800)] = gs
    return out


def box_headers(race):
    """Header pointers (list entry address, frame header) of every mode 0/2 animation list of
    every tag the race walk reaches: 0x1b12a reads the header bytes +4..+7 for the draw list
    box of any slot in the list."""
    import objects_data as od
    _, roots = od.scripts_of(race)
    _, anims, _ = od.walk(roots)
    out = set()
    for lst, stride, tag, mode in anims:
        if mode == 1: continue
        k = 0
        while rom_l(lst + stride * k) != 0xffffffff:
            out.add((lst + stride * k, rom_l(lst + stride * k))); k += 1
    return out


def cmd_data():
    """Per race: drawn tags, frame headers, frame bytes, MO tiles, palette sets."""
    allt = set()
    for race in range(6):
        fr = race_frames(race)
        if not fr: continue
        heads, tiles, sets, nbytes = set(), set(), set(), 0
        tags = sorted({k[0] for k in fr})
        for (tag, lst, orm), hs in fr.items():
            nbytes += 4 * len(hs) + 4
            for h in hs:
                n, t = frame_tiles(h)
                if h not in heads: nbytes += n
                heads.add(h); tiles |= t
                sets.add(((orm if orm is not None else (0x3800 if h >= 0x220ee else 0x2800)) >> 11) & 7)
        if 6 in tags:
            a, _, n = WAVE_STRIP
            for i in range(n):
                w = rom_w(a + 2 * i)
                if w != 0xffff: tiles.update(((w & 0x7ff) + t) for t in range(((w >> 11) & 7) + 1))
            nbytes += 3 * n
        allt |= tiles
        print("race %d (%s): tags %s, %d frame headers, %d B frame data, %d MO tiles "
              "(%d B at 4 bpp), palette sets %s" % (race, ms.md.NAMES[race], [hex(t) for t in tags],
                                                     len(heads), nbytes, len(tiles), 32 * len(tiles),
                                                     sorted(sets)))
    print("all races: %d distinct MO tiles, %d B at 4 bpp" % (len(allt), 32 * len(allt)))
    return True


# ------------------------------------------------------------------ draw list upkeep

class DrawList:
    """Slot hooks of marble_sim (Mem.drawlist): 0x18e6c insert and 0x18f46 remove on the work
    RAM list 0x4003bc and its 31 entries at 0x4001dc."""
    def init(self, m):
        """0x28ca6 list part: entries free, list empty, then the player (type 1, index 0)."""
        for k in range(NENTRIES): m.wb(ENTRIES + ENTRY_SIZE * k, 0)
        for k in range(NENTRIES): m.wb(LIST + k, 0xff)
        self.insert(m, 1, 0)

    def insert(self, m, typ, idx):
        """Sorted insert (0x18e6c): before the first entry that 0x1a80a puts after the new
        box (op 0 time, animation still empty), else at the end; only type and index of the
        entry are stored, the box is refreshed by the next 0x26f3e."""
        box = list(box_1b12a(m, typ, idx))
        end = LIST + LIST_MAX
        a3 = LIST
        while m.rb(a3) != 0xff and a3 < end:
            if cmp_boxes(boxw(m, entry(m, m.rb(a3))), box): break
            a3 += 1
        if a3 >= end: return
        a1, d1 = ENTRIES, 0
        while m.rb(a1) != 0 and a1 < ENTRIES + 0x1b2:
            a1 += ENTRY_SIZE; d1 += 1
        if a1 >= ENTRIES + 0x1b2: return
        m.wb(a1, typ); m.wb(a1 + 1, idx)
        if m.rb(a3) != 0xff:
            a1 = a3 + 1
            while m.rb(a1) != 0xff and a1 < end: a1 += 1
            a1 -= 1
            while a1 >= a3:
                if a1 < end - 1: m.wb(a1 + 1, m.rb(a1))
                a1 -= 1
        m.wb(a3, d1)

    def remove(self, m, typ, idx):
        """0x18f46: entry of (typ, idx) freed, the list closed up, 0xff at the new end."""
        end = LIST + LIST_MAX
        a1 = LIST
        while m.rb(a1) != 0xff and a1 < end:
            e = entry(m, m.rb(a1))
            if m.rb(e) == typ and m.rb(e + 1) == idx: break
            a1 += 1
        if a1 >= end or m.rb(a1) == 0xff: return
        m.wb(entry(m, m.rb(a1)), 0)
        a3 = a1 + 1
        while m.rb(a3) != 0xff and a3 < end: a3 += 1
        a3 -= 1
        while a1 < a3:
            m.wb(a1, m.rb(a1 + 1)); a1 += 1
        m.wb(a1, 0xff)


# ------------------------------------------------------------------ remake sprite data

NONE32 = 0xffffffff
STRIP_N = 23                    # wave strip entries (0x20f64 words, list 0x2227a frames)
SET_SHADE = 8                   # published set of the wave strip (priority MOs, shade colours)
SETS = (3, 5, 6, 7)             # MO palette sets of the level object frames
MAX_OBJS = 8                    # published objects per tick (fixture asserts)
NO_VARIANT = 0xff               # published tag 3 palette variant: no lift frame drawn
TAG3_FRAMES = 14                # 0x1df18: 7 frames of list 0x21156, 7 of 0x21192
CULL_MARGIN = 0                 # extra lines outward on every arcade cull (remake: 24)


def frame_cols(hdr):
    """MO frame header -> columns [(x, y, h, hflip, code)]: x = xb + dx + 8 k (k steps -8 when
    flipped), y = yb + dy (MO y offset above sy + 16), h tiles, code word."""
    P = ms.PROG
    xb, yb = s8(P[hdr]), s8(P[hdr + 1])
    b = rom_l(hdr + 8)
    flip = b & 1
    b &= ~1
    step = -8 if flip else 8
    out = []
    if P[b] == 0xff:
        n, dx, p = P[b + 2], s8(P[b + 3]), b + 4
        for k in range(n):
            out.append((xb + dx + step * k, yb + s8(P[p + 1]), P[p] + 1, flip, rom_w(p + 2)))
            p += 4
    else:
        n, dx, h, dy = P[b], s8(P[b + 1]), P[b + 2] + 1, s8(P[b + 3])
        for k in range(n):
            out.append((xb + dx + step * k, yb + dy, h, flip, rom_w(b + 4 + 2 * k)))
    return out


def strip_cols(i):
    """Wave strip entry i (0x27fa2): 4 priority columns of one tile code, 8 px right and 4
    lines up per column from the slot (no entry: none)."""
    w = rom_w(0x20f64 + 2 * i)
    if w == 0xffff: return []
    h, yo = ((w >> 11) & 7) + 1, s8(ms.PROG[0x20f92 + i])
    return [(8 + 8 * k, -0x40 + yo + 4 * k, h, 0, w & 0x7ff) for k in range(4)]


# steelie MO frame (type 2, set 5): in every race set (4 tiles), so races keep sharing sets
STEELIE_FRAME = 0x21f36
# slinky animation lists (type 4, set 4, 0x15460): their 51 frames (161 tiles) are in the
# shared death set (one copy for the three slinky races, set 4 palette equal in all three)
SLINKY_LISTS = (0x20c18, 0x20c48, 0x20c6c, 0x20c90, 0x20cb4, 0x20cd8, 0x20d64, 0x20d6c,
                0x20dd8, 0x20dec, 0x20e00, 0x20e14, 0x20e28)


class Sprites:
    """Level object sprite data of one race (MARBLE.md "Level object sprites"): frame ids
    (sorted header addresses, then the wave strip entries), columns, tile codes. The
    converter emits it, the publish model and the fixture use the same ids."""
    def __init__(self, race, steelies=True):
        fr = race_frames(race)
        self.race = race
        self.hdrs = sorted({h for hs in fr.values() for h in hs if h != NONE32}
                           | ({STEELIE_FRAME} if steelies else set()))
        self.ids = {h: i for i, h in enumerate(self.hdrs)}
        self.strip = len(self.hdrs)
        self.cols = [frame_cols(h) for h in self.hdrs]
        if any(k[0] == 6 and k[1] == 0x2227a for k in fr):
            self.cols += [strip_cols(i) for i in range(STRIP_N)]
        self.tile_set()

    def tile_set(self, extra=()):
        """Tile codes of every column (and the codes extra), sorted; tile_of: code -> index."""
        codes = set(extra)
        for cs in self.cols:
            for x, y, h, flip, code in cs:
                if code > 0x7ff or (code & 0x7ff) + h > 0x800:
                    raise SystemExit("marble: frame column code %04x height %d" % (code, h))
                codes.update(code + t for t in range(h))
        self.tile_codes = sorted(codes)
        self.tile_of = {c: i for i, c in enumerate(self.tile_codes)}

    def col_y(self, c):
        """Race row offset of a column from the entry y: -(MO y offset + 8 h)."""
        return -(c[1] + 8 * c[2])

    def objs_cols(self, frame):
        """Columns of a frame as the remake stores them: (x, y, h | hflip << 7, tile)."""
        assert all(c[2] <= 127 for c in self.cols[frame])  # h shares the byte with hflip
        return [(c[0], self.col_y(c), c[2] | c[3] << 7, self.tile_of[c[4]])
                for c in self.cols[frame]]


DEATH_LISTS = (ms.ANIM_DIZZY, ms.ANIM_DIZZY_LOOP, ms.ANIM_DIZZY_END, ms.ANIM_SHATTER,
               ms.ANIM_DISSOLVE)   # player +0x5a lists: dizzy and eaten, shatter, dissolve
SHATTER_TICKS = 2                  # ticks per shatter frame (0x25bae: +0x60 = 2)
STAR_CODE, STARS = 0x10b, 8        # respawn sparkle MOs: code STAR_CODE + k, height 1 (0x13ade)


def anim_list(a):
    """Frame headers of an animation list up to its 0xffffffff end."""
    out = []
    while rom_l(a + 4 * len(out)) != NONE32: out.append(rom_l(a + 4 * len(out)))
    return out


class DeathSprites(Sprites):
    """Player death frames of every race (MARBLE.md "Phase 4b-0 data"): the +0x5a lists and
    the shatter overlay entries 0x25fc2 reaches (+0x62 steps once per tick from shatter frame
    SHATTER_SHARDS + 1 to the list end), header frames only. The tiles also hold the sparkle
    star codes, consecutive. slinkies: also the slinky frames (Phase 4b-2)."""
    def __init__(self, slinkies=True):
        hdrs = {h for a in DEATH_LISTS for h in anim_list(a)}
        if slinkies: hdrs |= {h for a in SLINKY_LISTS for h in anim_list(a)}
        self.shards = (len(anim_list(ms.ANIM_SHATTER)) - ms.SHATTER_SHARDS - 1) * SHATTER_TICKS
        hdrs |= set(anim_list(ms.ANIM_SHARDS)[:self.shards])
        self.race = None
        self.hdrs = sorted(hdrs)
        self.ids = {h: i for i, h in enumerate(self.hdrs)}
        self.strip = len(self.hdrs)
        self.cols = [frame_cols(h) for h in self.hdrs]
        self.tile_set(range(STAR_CODE, STAR_CODE + STARS))


def slot_objs(m, sp, typ, idx, top, margin):
    """One drawn slot -> ([(frame id, set, x, y)], lift palette variant). Race pixel
    position of the slot origin: x = sx (tag 5: sx - 1), y = top + 240 - sy; the frame
    columns add their offsets. Culls as 0x26f3e (slot MOs above), moved outward by margin."""
    a = rom_l(0x1f016 + 4 * idx)
    d5, d4 = pos(m, a, 0x4e, 0x17 if typ == 5 else 0x18)
    ex, ey, cy = s16(d5) - 24, top + 240 - s16(m.rw(a + 0x50)), s16(d4)

    def one(hdr, st):
        if hdr == NONE32: return []
        return [(sp.ids[hdr], st, ex, ey)]

    def hdr_of(p): return m.rl(m.rl(p))

    if typ == 5:
        if cy <= -0x40 - margin or cy >= 0x100 + margin: return [], NO_VARIANT
        return one(hdr_of(a + 0x42), 3), NO_VARIANT
    if typ == 3:
        if cy <= -0x40 - margin or cy >= 0x160 + margin: return [], NO_VARIANT
        out = []
        for g in (0x40044a, 0x40044e, 0x400452): out += one(hdr_of(g), 7)
        own = hdr_of(a + 0x42)
        out += one(own, 6)
        i = ((m.rl(a + 0x42) - m.rl(a + 0x46)) >> 3) & 0xffff
        if m.rl(a + 0x46) == 0x21192: i += 7
        return out, (ms.PROG[TAG3_PAL + i] if own != NONE32 and i < TAG3_FRAMES else NO_VARIANT)
    # tag 6
    if cy <= -0x40 - margin or cy >= 0x140 + margin: return [], NO_VARIANT
    p42, p46 = m.rl(a + 0x42), m.rl(a + 0x46)
    hdr = m.rl(p42)
    out = one(hdr, 7 if hdr >= 0x220ee else 5)
    if p46 == 0x2227a:
        i = s16(((p42 - p46) >> 2) & 0xffff)
        if 0 <= i < STRIP_N and sp.cols[sp.strip + i]: out.append((sp.strip + i, SET_SHADE, ex, ey))
    return out, NO_VARIANT


def publish(m, sp, cam, margin=0):
    """Published object list of one tick (draw list order, back to front): (objs, marble_at,
    variant). objs: (frame id, set, x, y) in race pixels; marble_at: objs drawn before the
    marble (the marble's masks and body sit between objs[:marble_at] and the rest);
    variant: tag 3 palette variant of the drawn lift frame (0xff: none). cam: camera word
    0x40097e as the slot update saw it (before the tick's camera update)."""
    top = s16(cam) - m.sw(m.t.desc + 0x10)
    out, at, variant = [], None, NO_VARIANT
    for k in draw_list(m):
        e = entry(m, k)
        typ, idx = m.rb(e), m.rb(e + 1)
        if typ == 1 and idx == 0 and at is None: at = len(out)
        if typ not in SLOT_TYPES: continue
        objs, v = slot_objs(m, sp, typ, idx, top, margin)
        out += objs
        if v != NO_VARIANT: variant = v
    if len(out) > MAX_OBJS:
        raise SystemExit("marble: %d published objects, MAX_OBJS %d" % (len(out), MAX_OBJS))
    return out, len(out) if at is None else at, variant


def expand(sp, objs, top):
    """Published objects -> comparable MO columns [(x, screen line, h, hflip, tile code, set,
    priority)] with x and line modulo 512, the form of the MAME MO words."""
    out = []
    for fid, st, ex, ey in objs:
        for x, y, hh, tile in sp.objs_cols(fid):
            h = hh & 0x7f
            out.append(((ex + x + 24) & 0x1ff, (ey + y - top) & 0x1ff, h, hh >> 7,
                        sp.tile_codes[tile], 0 if st == SET_SHADE else st, int(st == SET_SHADE)))
    return out


def mo_cols(lst):
    """mo_list_26f3e slot entries -> the same column tuples (shade: set 0)."""
    out = []
    for _, typ, _, mos in lst:
        if typ not in SLOT_TYPES: continue
        for w0, code, w2 in mos:
            h = (w0 & 15) + 1
            out.append(((w2 >> 5) & 0x1ff, (-((w0 >> 5) & 0x1ff) - 256 - 8 * h) & 0x1ff, h,
                        w0 >> 15, code & 0x7ff, 0 if w2 & 0x8000 else (code >> 11) & 7,
                        w2 >> 15))
    return out


def check_publish(ram, sp, objs, top):
    """Published objects expand to the MO words of the validated model (Ram of the same
    state)."""
    return expand(sp, objs, top) == mo_cols(mo_list_26f3e(ram))


SWEEP_STEP = 4                  # camera step of the race sweep, lines
SWEEP_MARGIN = 24               # remake cull margin


def race_sweep(m, race, sp):
    """Draw list sweep of one race on m after ms.start_race and ms.snapshot_start (objects,
    sprites and DrawList on): the camera visits the race in SWEEP_STEP lines (top to bottom,
    silly bottom to top), then back, then again, the marble idle. Per tick: slot update, camera and band
    spawns (ms.tick), box refresh and sort, publish at the remake margin. Returns (largest
    published object count, the published lists per tick)."""
    top, rng = ms.rom_w(m.t.desc + 0x10), ms.rom_w(m.t.desc + 0x12)
    cams = list(range(top, top + rng + 1, SWEEP_STEP))
    if race == 4: cams.reverse()
    cams = cams + cams[::-1] + cams
    most, got = 0, []
    for cam in cams:
        before = m.rw(0x40097e)
        ms.tick(m, 0, 0, camera=cam)
        refresh_sort(m)
        got.append(publish(m, sp, before, SWEEP_MARGIN))
        most = max(most, len(got[-1][0]))
    return most, got


def refresh_sort(m):
    """0x26f3e head: box refresh and the three sort passes."""
    boxes_and_sort(m)


# ------------------------------------------------------------------ sprite fixture

def load_spritefixture(path):
    """Segments of source/test/test_marble_sprites/marble_sprites_trace.h:
    [{"race", "frame", "ram": [(addr, bytes)], "ticks": [(c6, c7, camera, n, marble_at,
    variant, first)], "objs": [(frame, set, x, y)]}]."""
    import re
    text = open(path).read()
    num = lambda s: [int(v, 0) for v in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", s)]
    arrays = {name: body for name, body in
              re.findall(r"static const \w+ (\w+)\[\] = \{(.*?)\n?\};", text, re.S)}
    segs = []
    for race, frame, nram, ramname, nticks, tickname, nobjs, objname in re.findall(
            r"\{ *(\d+), *(\d+), *(\d+), *(\w+), *(\d+), *(\w+), *(\d+), *(\w+) *\}",
            arrays["marble_sprites_segments"]):
        ram = []
        for addr, length, data in re.findall(r"\{ *(0x[0-9a-f]+), *(\d+), *(\w+) *\}",
                                             arrays[ramname]):
            b = bytes(num(arrays[data]))
            assert len(b) == int(length)
            ram.append((int(addr, 0), b))
        v, o = num(arrays[tickname]), num(arrays[objname])
        ticks = [tuple(v[k:k + 7]) for k in range(0, len(v), 7)]
        objs = [tuple(o[k:k + 4]) for k in range(0, len(o), 4)]
        assert len(ram) == int(nram) and len(ticks) == int(nticks) and len(objs) == int(nobjs)
        segs.append({"race": int(race), "frame": int(frame), "ram": ram, "ticks": ticks,
                     "objs": objs})
    return segs


def replay_spritefixture(seg, m, sp, margin=0):
    """Run every tick on m (objects, sprites and the draw list on): the sim tick, then
    0x26f3e (box refresh, sort) and publish. Returns (index of the first tick whose published
    list differs or None, [(objs, marble_at, variant)] per tick)."""
    m.objects = m.sprites = True
    m.drawlist = DrawList()
    bad, got = None, []
    for k, (c6, c7, cam, n, at, variant, first) in enumerate(seg["ticks"]):
        cam_before = m.rw(0x40097e)
        ms.tick(m, c6 & 0xff, c7 & 0xff, camera=cam & 0xffffffff)
        refresh_sort(m)
        got.append(publish(m, sp, cam_before, margin))
        want = ([tuple(o) for o in seg["objs"][first:first + n]], at, variant)
        if bad is None and got[-1] != want: bad = k
    return bad, got


def cmd_spritefixture(path):
    ok = True
    for seg in load_spritefixture(path):
        m = ms.fixture_mem(seg, ms.Terrain(seg["race"]))
        bad, _ = replay_spritefixture(seg, m, Sprites(seg["race"]))
        print("sprites fixture race %d frame %d: %d ticks, %s" % (seg["race"], seg["frame"],
              len(seg["ticks"]), "exact" if bad is None else "differs at tick %d" % bad))
        ok &= bad is None
    return ok


def main(argv):
    if len(argv) == 3 and argv[1] == "spritefixture":
        return 0 if cmd_spritefixture(argv[2]) else 1
    if len(argv) == 2 and argv[1] == "data":
        return 0 if cmd_data() else 1
    if len(argv) >= 6 and argv[1] == "camcheck":
        return 0 if cmd_camcheck(argv) else 1
    if len(argv) >= 7 and argv[1] == "render":
        return 0 if cmd_render(argv) else 1
    if len(argv) >= 4 and argv[1] in ("check", "sort"):
        trace = ms.load_trace(argv[2]); race = int(argv[3])
        ok = cmd_check(trace, race) if argv[1] == "check" else \
            cmd_sort(trace, race, [int(a) for a in argv[4:]])
        return 0 if ok else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
