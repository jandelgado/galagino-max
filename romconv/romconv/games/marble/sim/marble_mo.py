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
"""
from romconv.core.errors import ConversionError
from romconv.games.marble.sim import marble_sim as ms
from romconv.games.marble.sim.marble_sim import s8, s16

LIST, _LIST_END = 0x4003bc, 0x4003dc     # draw list bytes, 0xff end
ENTRIES, _ENTRY_SIZE, _NENTRIES = 0x4001dc, 14, 31    # entry k = ENTRIES + 14 k (RECTS[k])
_LIST_MAX = 0x1f                         # list bytes before the sentinel limit (0x18e6c)
SLOT_TYPES = (3, 5, 6, 10, 11, 12, 13)  # level object slot tags drawn by 0x26f3e


# ------------------------------------------------------------------ 0x1a8d2 MO frame block


def frame_1a8d2(rom, hdr, x, y, orm):
    """MO frame header at hdr (0xffffffff: nothing) at bias (x, y): list of (w0, code, w2).
    Header: s8 x bias, s8 y bias, 2 bytes, 4 box bytes (0x1b12a: dx, dy, sx, sy), long body
    (bit 0 = hflip, columns step -8 px). Body 0xff: short form, byte skipped, count, s8 dx,
    then count x (height - 1, s8 dy, word code); else long form: count, s8 dx, height - 1,
    s8 dy, count codes. One MO per column, 8 px apart, code | orm."""
    if hdr == 0xffffffff: return []
    x = (x + s8(rom.prog[hdr])) & 0xffff; y = (y + s8(rom.prog[hdr + 1])) & 0xffff
    b = rom.l(hdr + 8)
    flip, step = (0x8000, -8) if b & 1 else (0, 8)
    b &= ~1
    out = []
    if rom.prog[b] == 0xff:
        n = rom.prog[b + 2]; x = (x + s8(rom.prog[b + 3])) & 0xffff; p = b + 4
        for k in range(n):
            h, dy, code = rom.prog[p], s8(rom.prog[p + 1]), rom.w(p + 2); p += 4
            out.append((((y + dy) << 5) & 0x3fe0 | flip | h, code | orm, ((x + step * k) << 5) & 0x3fe0))
    else:
        n, dx, h, dy = rom.prog[b], s8(rom.prog[b + 1]), rom.prog[b + 2], s8(rom.prog[b + 3])
        x = (x + dx) & 0xffff; w0 = ((y + dy) << 5) & 0x3fe0 | flip | h
        for k in range(n):
            out.append((w0, rom.w(b + 4 + 2 * k) | orm, ((x + step * k) << 5) & 0x3fe0))
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
        a2 = m.rom_l((0x1f006 if typ == 4 else 0x1f07a) + 4 * idx)
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
        a1 = m.rom_l(0x1f096 + 4 * idx)
        dx, dy, sx, sy = sub(m.rl(m.rl(a1 + 0x1c)))
        dz, sz = (-8, 0) if m.rb(a1 + 0x1a) == 2 else (0, 6)
        return obj(a1, dx, dy, dz, sx, sy, sz)
    if typ == 0xf:
        a1 = m.rom_l(0x1f0ba + 4 * idx)
        if m.rb(a1 + 0x1a) == 2: return obj(a1, 0, 0, 0, 0, 0, 0)
        return obj(a1, -4, -4, 0, 8, 8, 4)
    a1 = m.rom_l(0x1f016 + 4 * idx)                   # level object slot, tag = type
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
    return ENTRIES + _ENTRY_SIZE * k


def _boxw(m, e):
    return [s16(m.rw(e + 2 + 2 * i)) for i in range(6)]


def _cmp_boxes(p, q):
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
    return _cmp_boxes(_boxw(m, a1), _boxw(m, a0))


def _sort_1a7a8(m, stride):
    for i in range(0x20 - stride):
        a, b = LIST + i, LIST + i + stride
        if m.rb(a) == 0xff or m.rb(b) == 0xff: return
        if cmp_1a80a(m, entry(m, m.rb(a)), entry(m, m.rb(b))):
            va, vb = m.rb(a), m.rb(b); m.wb(a, vb); m.wb(b, va)


def draw_list(m):
    out = []
    a = LIST
    while m.rb(a) != 0xff and a < _LIST_END - 1:
        out.append(m.rb(a)); a += 1
    return out


def boxes_and_sort(m):
    """0x26f3e phases 1-2: refresh every entry box (0x1b12a), 3 sort passes (0x4003e2 == 0)."""
    for k in draw_list(m):
        e = entry(m, k)
        for i, v in enumerate(box_1b12a(m, m.rb(e), m.rb(e + 1))):
            m.wb(e + 2 + 2 * i, (v & 0xffff) >> 8); m.wb(e + 3 + 2 * i, v & 0xff)
    if m.rb(0x4003e2) == 0:
        for s in (1, 2, 3): _sort_1a7a8(m, s)


# ------------------------------------------------------------------ 0x26f3e MO list


def pos(m, a, off, xb=0x18, yb=0x10):
    return (m.rw(a + off) + xb) & 0xffff, (m.rw(a + off + 2) + yb) & 0xffff


def mo(x, y, code, w0bits=0, w2bits=0):
    return ((y << 5) & 0x3fe0 | w0bits, code & 0xffff, (x << 5) & 0x3fe0 | w2bits)


def _entries6(m, a, n, xflag=None, setbits=0):
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
    a = m.rom_l(0x1eff6 + 4 * idx)
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
        out += frame_1a8d2(m.rom, m.rl(m.rl(a + 0x5a)), d5, d4, f10)
    else:
        out += _entries6(m, a + 0xa4, 4, 0, f10)
        if st not in (4, 10, 11) and not (st == 8 and fn1):
            e3 = ext(f10) | 7
            out.append(mo(s16(d5) - 8, s16(d4) + 5, e3 & 0xffff, (e3 >> 16) & 0xffff))
    out += _entries6(m, a + 0x38, 5, "mask", f10)
    if m.rb(a + 0x67): out += frame_1a8d2(m.rom, m.rl(m.rl(a + 0x62)), d5, d4, 0x1000)
    if m.rb(a + 0xd1): out += frame_1a8d2(m.rom, m.rl(m.rl(a + 0xcc)), d5, d4, 0x1000)
    if m.rb(a + 0xd8) and st not in (4, 10, 11, 7, 2):
        out += frame_1a8d2(m.rom, m.rl(a + 0xd4), (d5 + s8(m.rb(a + 0x68))) & 0xffff,
                           (d4 + (0x20 if st == 6 else 0x10)) & 0xffff, f10)
    return out


def other_mos(m, typ, idx):
    """Enemies and the rest (Marble Love), only to keep the list aligned."""
    if typ == 2:
        a = m.rom_l(0x1effe + 4 * idx)
        if not m.rb(a + 0x1c): return []
        d5, d4 = pos(m, a, 0x1e)
        hdr = 0x21f36 if m.rb(a + 0x18) != 2 else m.rl(m.rl(a + 0x5a))
        out = frame_1a8d2(m.rom, hdr, d5, d4, 0x2800) + _entries6(m, a + 0x38, 5, 0x8000)
        if m.rb(a + 0x67): out += frame_1a8d2(m.rom, m.rl(m.rl(a + 0x62)), d5, d4, 0x1000)
        return out
    if typ == 4:
        a = m.rom_l(0x1f006 + 4 * idx)
        d5, d4 = pos(m, a, 0x28)
        if s16(d4) <= -0x20 or s16(d4) >= 0x100: return []
        return frame_1a8d2(m.rom, m.rl(m.rl(a + 0x58)), d5, d4, 0x2000) + _entries6(m, a + 0x2c, 5, 0x8000)
    if typ in (7, 8, 9):
        a = m.rom_l(0x1f096 + 4 * idx)
        d5, d4 = pos(m, a, 0x20)
        if s16(d4) <= -0x10 or s16(d4) >= 0x100: return []
        return frame_1a8d2(m.rom, m.rl(m.rl(a + 0x1c)), d5, d4, {7: 0x2800, 8: 0x3000, 9: 0x2000}[typ])
    if typ == 14:
        a = m.rom_l(0x1f07a + 4 * idx)
        d5, d4 = pos(m, a, 0x28)
        if s16(d4) <= -0x30 or s16(d4) >= 0x120: return []
        return frame_1a8d2(m.rom, m.rl(m.rl(a + 0x3a)), d5, d4, 0x3000)
    if typ == 15:
        a = m.rom_l(0x1f0ba + 4 * idx)
        d5, d4 = pos(m, a, 0x20)
        if s16(d4) <= -0x10 or s16(d4) >= 0x100: return []
        return frame_1a8d2(m.rom, m.rl(m.rl(a + 0x1c)), d5, d4, 0x3800) + _entries6(m, a + 0x26, 3, 0x8000)
    if typ == 0x29:
        a = 0x401650 + (idx << 4)
        d5, d4 = pos(m, a, 0xc)
        if s16(d4) <= -0x40 or s16(d4) >= 0x100: return []
        return frame_1a8d2(m.rom, m.rl(m.rl(a + 8)), d5, d4, 0x2000)
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


# ------------------------------------------------------------------ render check

PAL_CMDS = 0x20ae4      # palette command -> descriptor (count, dest offset, words)
PAL_DESTS = 0x20840     # palette RAM address per descriptor word
TAG3_PAL = 0x1df18      # tag 3 frame index (+7 for list 0x21192) -> palette command


_TAG3_GLOBALS = (0x211fe, 0x2121e, 0x2126e, 0x212b2)   # 0x40044a/e/52 lists (set 7)


def race_frames(rom, race):
    """Drawn slot tags of one race -> {(list, orm): [headers]} (mode 0 lists hold headers,
    mode 2 pairs (header, terrain block))."""
    from romconv.games.marble.sim import objects_data as od
    _, roots = od.scripts_of(rom, race)
    _, anims, _ = od.walk(rom, roots)
    out = {}
    for lst, stride, tag, mode in sorted(anims):
        if tag not in SLOT_TYPES or mode == 1: continue
        hs = []
        while rom.l(lst + stride * len(hs)) != 0xffffffff: hs.append(rom.l(lst + stride * len(hs)))
        orm = {3: 0x3000, 5: 0x1800, 6: None, 10: 0x3000, 11: 0x1800, 12: 0x3800, 13: 0x1800}[tag]
        out[(tag, lst, orm)] = hs
        if tag == 3:
            for g in _TAG3_GLOBALS:
                gs = []
                while rom.l(g + 4 * len(gs)) != 0xffffffff: gs.append(rom.l(g + 4 * len(gs)))
                out[(3, g, 0x3800)] = gs
    return out


def box_headers(rom, race):
    """Header pointers (list entry address, frame header) of every mode 0/2 animation list of
    every tag the race walk reaches: 0x1b12a reads the header bytes +4..+7 for the draw list
    box of any slot in the list."""
    from romconv.games.marble.sim import objects_data as od
    _, roots = od.scripts_of(rom, race)
    _, anims, _ = od.walk(rom, roots)
    out = set()
    for lst, stride, tag, mode in anims:
        if mode == 1: continue
        k = 0
        while rom.l(lst + stride * k) != 0xffffffff:
            out.add((lst + stride * k, rom.l(lst + stride * k))); k += 1
    return out


# ------------------------------------------------------------------ draw list upkeep


class DrawList:
    """Slot hooks of marble_sim (Mem.drawlist): 0x18e6c insert and 0x18f46 remove on the work
    RAM list 0x4003bc and its 31 entries at 0x4001dc."""
    def init(self, m):
        """0x28ca6 list part: entries free, list empty, then the player (type 1, index 0)."""
        for k in range(_NENTRIES): m.wb(ENTRIES + _ENTRY_SIZE * k, 0)
        for k in range(_NENTRIES): m.wb(LIST + k, 0xff)
        self.insert(m, 1, 0)

    def insert(self, m, typ, idx):
        """Sorted insert (0x18e6c): before the first entry that 0x1a80a puts after the new
        box (op 0 time, animation still empty), else at the end; only type and index of the
        entry are stored, the box is refreshed by the next 0x26f3e."""
        box = list(box_1b12a(m, typ, idx))
        end = LIST + _LIST_MAX
        a3 = LIST
        while m.rb(a3) != 0xff and a3 < end:
            if _cmp_boxes(_boxw(m, entry(m, m.rb(a3))), box): break
            a3 += 1
        if a3 >= end: return
        a1, d1 = ENTRIES, 0
        while m.rb(a1) != 0 and a1 < ENTRIES + 0x1b2:
            a1 += _ENTRY_SIZE; d1 += 1
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
        end = LIST + _LIST_MAX
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

_NONE32 = 0xffffffff
_STRIP_N = 23                    # wave strip entries (0x20f64 words, list 0x2227a frames)
SET_SHADE = 8                   # published set of the wave strip (priority MOs, shade colours)
SETS = (3, 5, 6, 7)             # MO palette sets of the level object frames
_MAX_OBJS = 8                    # published objects per tick (fixture asserts)
_NO_VARIANT = 0xff               # published tag 3 palette variant: no lift frame drawn
TAG3_FRAMES = 14                # 0x1df18: 7 frames of list 0x21156, 7 of 0x21192


def frame_cols(rom, hdr):
    """MO frame header -> columns [(x, y, h, hflip, code)]: x = xb + dx + 8 k (k steps -8 when
    flipped), y = yb + dy (MO y offset above sy + 16), h tiles, code word."""
    P = rom.prog
    xb, yb = s8(P[hdr]), s8(P[hdr + 1])
    b = rom.l(hdr + 8)
    flip = b & 1
    b &= ~1
    step = -8 if flip else 8
    out = []
    if P[b] == 0xff:
        n, dx, p = P[b + 2], s8(P[b + 3]), b + 4
        for k in range(n):
            out.append((xb + dx + step * k, yb + s8(P[p + 1]), P[p] + 1, flip, rom.w(p + 2)))
            p += 4
    else:
        n, dx, h, dy = P[b], s8(P[b + 1]), P[b + 2] + 1, s8(P[b + 3])
        for k in range(n):
            out.append((xb + dx + step * k, yb + dy, h, flip, rom.w(b + 4 + 2 * k)))
    return out


def strip_cols(rom, i):
    """Wave strip entry i (0x27fa2): 4 priority columns of one tile code, 8 px right and 4
    lines up per column from the slot (no entry: none)."""
    w = rom.w(0x20f64 + 2 * i)
    if w == 0xffff: return []
    h, yo = ((w >> 11) & 7) + 1, s8(rom.prog[0x20f92 + i])
    return [(8 + 8 * k, -0x40 + yo + 4 * k, h, 0, w & 0x7ff) for k in range(4)]


# steelie MO frame (type 2, set 5): in every race set (4 tiles), so races keep sharing sets
STEELIE_FRAME = 0x21f36
# slinky animation lists (type 4, set 4, 0x15460): their 51 frames (161 tiles) are in the
# shared death set (one copy for the three slinky races, set 4 palette equal in all three)
SLINKY_LISTS = (0x20c18, 0x20c48, 0x20c6c, 0x20c90, 0x20cb4, 0x20cd8, 0x20d64, 0x20d6c,
                0x20dd8, 0x20dec, 0x20e00, 0x20e14, 0x20e28)
# acid animation lists (type 0x0e, set 6, 0x1d242): their 35 frames are in the shared death
# set as well (set 6 palette equal in both acid races)
ACID_LISTS = (0x20ea4, 0x20ec4, 0x20ee4, 0x20f24)
# silly race bird animation lists (type 0x0f, set 7, 0x19a40/0x19baa/0x19d94): spawn, walk,
# landing, carrying; their frames join the death set (palette set 7 of the silly race)
BIRD_LISTS = (0x224ca, 0x224d6, 0x224e2, 0x22546)
# silly race mini animation lists (types 7/8/9, sets 5/6/4: 0x1953e walk, 0x194ba collected,
# 0x1924e hit); their frames join the death set (palette sets of the silly race)
MINI_LISTS = (0x21f72, 0x21f7a, 0x21f8a, 0x21a62, 0x21efe, 0x219b2, 0x21a32, 0x2197a, 0x219ea,
              0x2194e, 0x21e3e, 0x21eb6, 0x21d4e, 0x21dc6, 0x21f06)
# aerial race piston animation list (type 0x29, set 4, 0x1844a); its frames join the death set
# (palette set 4 of the aerial race)
PISTON_LISTS = (0x21342,)


_SHARED_SETS = ((2, 3),)  # races drawn from one sprite set (intermediate waves, aerial objects)


class Sprites:
    """Level object sprite data of one race (MARBLE.md "Level object sprites"): frame ids
    (sorted header addresses, then the wave strip entries), columns, tile codes. The
    converter emits it, the publish model and the fixture use the same ids."""
    def __init__(self, rom, race, steelies=True):
        fr = {}
        for r in next((g for g in _SHARED_SETS if race in g), (race,)):
            fr.update(race_frames(rom, r))
        self.race = race
        self.hdrs = sorted({h for hs in fr.values() for h in hs if h != _NONE32}
                           | ({STEELIE_FRAME} if steelies else set()))
        self.ids = {h: i for i, h in enumerate(self.hdrs)}
        self.strip = len(self.hdrs)
        self.cols = [frame_cols(rom, h) for h in self.hdrs]
        if any(k[0] == 6 and k[1] == 0x2227a for k in fr):
            self.cols += [strip_cols(rom, i) for i in range(_STRIP_N)]
        self.tile_set()

    def tile_set(self, extra=()):
        """Tile codes of every column (and the codes extra), sorted; tile_of: code -> index."""
        codes = set(extra)
        for cs in self.cols:
            for x, y, h, flip, code in cs:
                if code > 0x7ff or (code & 0x7ff) + h > 0x800:
                    raise ConversionError("marble: frame column code %04x height %d" % (code, h))
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
_SHATTER_TICKS = 2                  # ticks per shatter frame (0x25bae: +0x60 = 2)
STAR_CODE, STARS = 0x10b, 8        # respawn sparkle MOs: code STAR_CODE + k, height 1 (0x13ade)


def anim_list(rom, a):
    """Frame headers of an animation list up to its 0xffffffff end."""
    out = []
    while rom.l(a + 4 * len(out)) != _NONE32: out.append(rom.l(a + 4 * len(out)))
    return out


class DeathSprites(Sprites):
    """Player death frames of every race (MARBLE.md "Phase 4b-0 data"): the +0x5a lists and
    the shatter overlay entries 0x25fc2 reaches (+0x62 steps once per tick from shatter frame
    SHATTER_SHARDS + 1 to the list end), header frames only. The tiles also hold the sparkle
    star codes, consecutive. slinkies: also the slinky frames (Phase 4b-2), acid: the acid
    frames (Phase 4b-3), birds: the silly race bird frames (Phase 4b-4a), minis: the silly
    race mini frames (Phase 4b-4b), pistons: the aerial race piston frames (Phase 4b-5, their
    headers sort before the player frames)."""
    def __init__(self, rom, slinkies=True, acid=True, birds=True, minis=True, pistons=True):
        hdrs = {h for a in DEATH_LISTS for h in anim_list(rom, a)}
        if slinkies: hdrs |= {h for a in SLINKY_LISTS for h in anim_list(rom, a)}
        if acid: hdrs |= {h for a in ACID_LISTS for h in anim_list(rom, a)}
        if birds: hdrs |= {h for a in BIRD_LISTS for h in anim_list(rom, a)}
        if minis: hdrs |= {h for a in MINI_LISTS for h in anim_list(rom, a)}
        if pistons: hdrs |= {h for a in PISTON_LISTS for h in anim_list(rom, a)}
        self.shards = (len(anim_list(rom, ms.ANIM_SHATTER)) - ms.SHATTER_SHARDS - 1) * _SHATTER_TICKS
        hdrs |= set(anim_list(rom, ms.ANIM_SHARDS)[:self.shards])
        self.race = None
        self.hdrs = sorted(hdrs)
        self.ids = {h: i for i, h in enumerate(self.hdrs)}
        self.strip = len(self.hdrs)
        self.cols = [frame_cols(rom, h) for h in self.hdrs]
        self.tile_set(range(STAR_CODE, STAR_CODE + STARS))


def slot_objs(m, sp, typ, idx, top, margin):
    """One drawn slot -> ([(frame id, set, x, y)], lift palette variant). Race pixel
    position of the slot origin: x = sx (tag 5: sx - 1), y = top + 240 - sy; the frame
    columns add their offsets. Culls as 0x26f3e (slot MOs above), moved outward by margin."""
    a = m.rom_l(0x1f016 + 4 * idx)
    d5, d4 = pos(m, a, 0x4e, 0x17 if typ == 5 else 0x18)
    ex, ey, cy = s16(d5) - 24, top + 240 - s16(m.rw(a + 0x50)), s16(d4)

    def one(hdr, st):
        if hdr == _NONE32: return []
        return [(sp.ids[hdr], st, ex, ey)]

    def hdr_of(p): return m.rl(m.rl(p))

    if typ == 5:
        if cy <= -0x40 - margin or cy >= 0x100 + margin: return [], _NO_VARIANT
        return one(hdr_of(a + 0x42), 3), _NO_VARIANT
    if typ == 3:
        if cy <= -0x40 - margin or cy >= 0x160 + margin: return [], _NO_VARIANT
        out = []
        for g in (0x40044a, 0x40044e, 0x400452): out += one(hdr_of(g), 7)
        own = hdr_of(a + 0x42)
        out += one(own, 6)
        i = ((m.rl(a + 0x42) - m.rl(a + 0x46)) >> 3) & 0xffff
        if m.rl(a + 0x46) == 0x21192: i += 7
        return out, (m.prog[TAG3_PAL + i] if own != _NONE32 and i < TAG3_FRAMES else _NO_VARIANT)
    if typ == 10:                                    # catapult arm
        if cy <= -0x40 - margin or cy >= 0x120 + margin: return [], _NO_VARIANT
        return one(hdr_of(a + 0x42), 6), _NO_VARIANT
    if typ in (11, 12, 13):                          # gates (set 3), bounce boxes (set 7); the
        if cy <= -0x20 - margin or cy >= 0x100 + margin: return [], _NO_VARIANT   # gates' two
        return one(hdr_of(a + 0x42), 7 if typ == 12 else 3), _NO_VARIANT  # priority MOs: not drawn
    # tag 6
    if cy <= -0x40 - margin or cy >= 0x140 + margin: return [], _NO_VARIANT
    p42, p46 = m.rl(a + 0x42), m.rl(a + 0x46)
    hdr = m.rl(p42)
    out = one(hdr, 7 if hdr >= 0x220ee else 5)
    if p46 == 0x2227a:
        i = s16(((p42 - p46) >> 2) & 0xffff)
        if 0 <= i < _STRIP_N and sp.cols[sp.strip + i]: out.append((sp.strip + i, SET_SHADE, ex, ey))
    return out, _NO_VARIANT


def publish(m, sp, cam, margin=0):
    """Published object list of one tick (draw list order, back to front): (objs, marble_at,
    variant). objs: (frame id, set, x, y) in race pixels; marble_at: objs drawn before the
    marble (the marble's masks and body sit between objs[:marble_at] and the rest);
    variant: tag 3 palette variant of the drawn lift frame (0xff: none). cam: camera word
    0x40097e as the slot update saw it (before the tick's camera update)."""
    top = s16(cam) - m.sw(m.t.desc + 0x10)
    out, at, variant = [], None, _NO_VARIANT
    for k in draw_list(m):
        e = entry(m, k)
        typ, idx = m.rb(e), m.rb(e + 1)
        if typ == 1 and idx == 0 and at is None: at = len(out)
        if typ not in SLOT_TYPES: continue
        objs, v = slot_objs(m, sp, typ, idx, top, margin)
        out += objs
        if v != _NO_VARIANT: variant = v
    if len(out) > _MAX_OBJS:
        raise ConversionError("marble: %d published objects, MAX_OBJS %d" % (len(out), _MAX_OBJS))
    return out, len(out) if at is None else at, variant


SWEEP_STEP = 4                  # camera step of the race sweep, lines
SWEEP_MARGIN = 24               # remake cull margin


def race_sweep(m, race, sp):
    """Draw list sweep of one race on m after ms.start_race and ms.snapshot_start (objects,
    sprites and DrawList on): the camera visits the race in SWEEP_STEP lines (top to bottom,
    silly bottom to top), then back, then again, the marble idle. Per tick: slot update, camera and band
    spawns (ms.tick), box refresh and sort, publish at the remake margin. Returns (largest
    published object count, the published lists per tick)."""
    top, rng = m.rom_w(m.t.desc + 0x10), m.rom_w(m.t.desc + 0x12)
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
    text = open(path, encoding="utf-8").read()
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
