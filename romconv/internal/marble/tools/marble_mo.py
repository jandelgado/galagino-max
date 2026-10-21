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

from devrom import ROM, SIM_ROM, md  # noqa: F401
from romconv.games.marble.sim import marble_mo as _model  # noqa: F401
from romconv.games.marble.sim.marble_mo import *  # noqa: F401,F403
import marble_sim as ms
from marble_sim import rom_w, rom_l, s8, s16

# The model takes its ROM explicitly (marble_sim.SimRom); the tools use the one loaded by devrom.
def frame_1a8d2(hdr, x, y, orm): return _model.frame_1a8d2(SIM_ROM, hdr, x, y, orm)
def race_frames(race): return _model.race_frames(SIM_ROM, race)
def box_headers(race): return _model.box_headers(SIM_ROM, race)
def frame_cols(hdr): return _model.frame_cols(SIM_ROM, hdr)
def strip_cols(i): return _model.strip_cols(SIM_ROM, i)
def anim_list(a): return _model.anim_list(SIM_ROM, a)
def Sprites(race, steelies=True): return _model.Sprites(SIM_ROM, race, steelies)
def DeathSprites(*args, **kwargs): return _model.DeathSprites(SIM_ROM, *args, **kwargs)



RECTS = 0x1f0e2                         # long per list byte -> 14 byte entry (type, index, box)


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
    import marble_occlusion as mocc
    from PIL import Image
    shots, snapdir, trace, race, outdir = argv[2], argv[3], ms.load_trace(argv[4]), int(argv[5]), argv[6]
    os.makedirs(outdir, exist_ok=True)
    rom = ROM
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
    import marble_occlusion as mocc
    from PIL import Image
    shots, snapdir, trace, race = argv[2], argv[3], ms.load_trace(argv[4]), int(argv[5])
    rom = ROM
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
WAVE_STRIP = (0x20f64, 0x20f92, 23)                    # tag 6 list 0x2227a: words, s8 dy


def frame_tiles(hdr):
    """Header -> (header + body bytes, MO tile codes (code & 0x7ff, every tile of a column))."""
    b = rom_l(hdr + 8) & ~1
    body = 4 + 4 * ms.PROG[b + 2] if ms.PROG[b] == 0xff else 4 + 2 * ms.PROG[b]
    tiles = set()
    for w0, code, _ in frame_1a8d2(hdr, 0, 0, 0):
        tiles.update((code + t) & 0x7ff for t in range((w0 & 15) + 1))
    return 12 + body, tiles


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
CULL_MARGIN = 0                 # extra lines outward on every arcade cull (remake: 24)


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
    """mo_list_26f3e slot entries -> the same column tuples (shade: set 0). The gates' two
    priority MOs (code 0x500: hide earlier MOs in the gate mouth) are not drawn by the remake
    (its masks hide only their owner) and left out."""
    out = []
    for _, typ, _, mos in lst:
        if typ not in SLOT_TYPES: continue
        for w0, code, w2 in mos:
            if typ in (11, 13) and w2 & 0x8000: continue
            h = (w0 & 15) + 1
            out.append(((w2 >> 5) & 0x1ff, (-((w0 >> 5) & 0x1ff) - 256 - 8 * h) & 0x1ff, h,
                        w0 >> 15, code & 0x7ff, 0 if w2 & 0x8000 else (code >> 11) & 7,
                        w2 >> 15))
    return out


def check_publish(ram, sp, objs, top):
    """Published objects expand to the MO words of the validated model (Ram of the same
    state)."""
    return expand(sp, objs, top) == mo_cols(mo_list_26f3e(ram))


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
