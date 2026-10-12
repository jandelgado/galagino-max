#!/usr/bin/env python3
"""Marble Madness level object data per race: what a remake port has to carry.

Walks the spawn lists (0x1dec0[race], entries lo band, hi band, long script; script 0 =
random group of 4 from 0x1ded8), every slot script reachable from them (byte code
0x12896, MARBLE.md "Level objects"), the animation lists the scripts select and the
frame records they hold, and the occlusion tables (0x264aa). Prints byte counts.

usage: objects_data.py [-v]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import marble_sim as ms
from marble_sim import PROG, rom_w, rom_l, s16

ARGS = {0: None, 1: 4, 2: 4, 3: 2, 4: 2, 5: 0, 6: 2, 7: 0, 8: 6, 9: 4, 10: 4, 11: 0,
        12: 4, 13: 4, 14: 4, 15: 0, 16: 0, 17: 2, 18: 4}


def scripts_of(race):
    a = rom_l(0x1dec0 + 4 * race)
    ents = []
    while PROG[a] != 0xff:
        ents.append((PROG[a], PROG[a + 1], rom_l(a + 2))); a += 6
    roots = []
    for lo, hi, sc in ents:
        roots += [rom_l(0x1ded8 + 4 * k) for k in range(16)] if sc == 0 else [sc]
    if race == 2: roots.append(0x1d854)              # wave, spawned by 0x12fd0
    if race == 3:                                    # aerial scripts entered by pc (mode 0
        roots += [(0x1d766, 0, 0x0b, None), (0x1d7ac, 0, 0x0d, None),  # slots), with their
                  (0x1d752, 0, 0x0b, None), (0x1d798, 0, 0x0d, None),  # tag: gate wait
                  (0x1db80, 0, 0x0a, None)]          # (0x13966), close (0x13892, 0x2563e),
                                                     # catapult throw (0x29e22)
    return ents, roots


def walk(roots, patch=None):
    """Script bytes, animation lists (pointer, frame stride), tags. patch: optional set,
    receives (list pointer, column, row) of mode 1 slots (op 0 and ops 12/13 lists)."""
    code, anims, tags, seen = set(), set(), set(), set()
    todo = [r if isinstance(r, tuple) else (r, 0, 0, None) for r in roots]  # (pc, mode, tag, row)
    while todo:
        pc, mode, tag, crow = todo.pop()
        loops = {}
        while (pc, mode) not in seen:
            seen.add((pc, mode))
            op = s16(rom_w(pc)); start = pc; pc += 2
            if op < 0 or op > 18: code.update(range(start, pc)); break
            if op == 0:
                mode = PROG[pc + 7]; tag = PROG[pc + 9]; tags.add(tag); pc += 10
                if mode == 1:
                    crow = (rom_w(pc), rom_w(pc + 2))
                    anims.add((rom_l(pc + 4), 8, tag, 1)); pc += 8
                    if patch is not None: patch.add((rom_l(pc - 4),) + crow)
            else:
                pc += ARGS[op]
            code.update(range(start, pc))
            if op in (4, 6): loops[op] = PROG[pc - 1]
            if op in (5, 7) and loops.get(op - 1) == 0: break     # count 0: loops forever
            if op in (12, 13):
                anims.add((rom_l(pc - 4), 8 if mode else 4, tag, mode))
                if patch is not None and mode == 1: patch.add((rom_l(pc - 4),) + crow)
            if op in (8, 10, 18): todo.append((rom_l(pc - 4), mode, tag, crow))
            if op == 9: pc = rom_l(pc - 4); continue
            if op in (11, 15): break
            if op == 3 and rom_w(pc - 2) == 0: break
            if op in (1, 2) and rom_w(pc - 4) == 0: break
    return code, anims, tags


def frames(anims):
    """Animation lists (records until 0xffffffff) -> list bytes, tile blocks, dyn blocks,
    MO frame pointers."""
    lists = tiles = dyn = 0
    mo = set(); tb = set(); db = set()
    for a, stride, tag, mode in anims:
        n = 0
        while rom_l(a + n * stride) != 0xffffffff and n < 500: n += 1
        lists += n * stride + 4
        for k in range(n):
            r = a + k * stride
            if stride == 4:
                mo.add(rom_l(r)); continue
            t, d = rom_l(r), rom_l(r + 4)
            if mode == 2: mo.add(t); t = 0               # mode 2: MO frame + terrain block
            if tag == 0x19: t = 0                    # ultimate row number, not a tile block
            if t and t not in tb:
                if t >= 0x30000: raise ValueError("tile block %06x outside program ROM" % t)
                tb.add(t); tiles += 2 + 2 * PROG[t] * PROG[t + 1]
            if d and d not in db:
                db.add(d); dyn += 4 + 2 * rom_w(d + 2)
    return lists, tiles, dyn, len(mo), len(tb), len(db)


def occlusion_tables():
    """0x1ea2a explicit occluders (36 columns x 6 races), terrain edge bitmaps (descriptor
    +0x2e, 36 columns), small tables."""
    expl = set()
    for c in range(36):
        tab = rom_l(0x1ea2a + 4 * c)
        for race in range(6):
            sel = rom_w(tab + 2 * race)
            if not sel: continue
            r = tab + 2 * sel
            while s16(rom_w(r)) != -1: expl.add(r); r += 10
    bitmaps = 0
    for race in range(6):
        desc = rom_l(0x2be00 + 4 * race)
        bitmaps += 36 * ((rom_w(desc + 0x18) + 7) // 8 + 1)
    small = 15 + 8 + 2 * 17 + 2 * 17 + 4 * 36 + 8 * 4 + 8 + 0x58
    return 10 * len(expl), bitmaps, small


def spans(addrs):
    """Byte addresses -> sorted [first, end) ranges."""
    out = []
    for a in sorted(addrs):
        if out and a == out[-1][1]: out[-1][1] = a + 1
        else: out.append([a, a + 1])
    return [tuple(r) for r in out]


def object_bytes(race):
    """Program ROM bytes the object slots of one race read: spawn list, scripts, animation
    lists, terrain blocks. Playfield tile blocks (never read by the sim, patched by the
    remake from `patches`) and MO frames (format 0x1a8d2, Phase 4) are not included."""
    ents, roots = scripts_of(race)
    code, anims, tags = walk(roots)
    out = set(code)
    a = rom_l(0x1dec0 + 4 * race)
    out.update(range(a, a + 6 * len(ents) + 1))
    if any(sc == 0 for _, _, sc in ents): out.update(range(0x1ded8, 0x1ded8 + 64))
    for lst, stride, tag, mode in anims:
        n = 0
        while rom_l(lst + n * stride) != 0xffffffff and n < 500: n += 1
        out.update(range(lst, lst + n * stride + 4))
        if stride != 8: continue
        for k in range(n):
            d = rom_l(lst + k * stride + 4)
            if d: out.update(range(d, d + 4 + 2 * rom_w(d + 2)))
    if race == 2: out.update(range(0x24c7a, 0x24e28))    # wave tables 0x1d06a
    return out


def object_tables():
    """Tables all races share: spawn list pointers, slot address table (0x12d6e), the three
    global animation lists tag 3 steps (0x40044a/e/52, wrap targets 0x211fe, 0x2121e,
    0x2126e, 0x212b2; 0x13068 also reads the long before the pointer)."""
    out = set(range(0x1dec0, 0x1dec0 + 24)) | set(range(0x1f016, 0x1f016 + 100))
    for a in (0x211fe, 0x2121e, 0x2126e, 0x212b2):
        e = a
        while rom_l(e) != 0xffffffff: e += 4
        out.update(range(a - 4, e + 4))
    return out


def occlusion_bytes():
    """Program ROM bytes 0x264aa reads (edges come from the converter edge lists): shape and mask tables, the
    vertex tables of 0x177f8, the explicit occluder lists (0x1ea2a)."""
    out = set(range(0x1eaba, 0x1eaba + 16))
    out.update(range(0x1ead2, 0x1ead2 + 34)); out.update(range(0x1eaf4, 0x1eaf4 + 34))
    out.update(range(0x24176, 0x24176 + 8 + 32))
    out.update(range(0x1ea2a, 0x1ea2a + 144))
    for c in range(36):
        tab = rom_l(0x1ea2a + 4 * c)
        out.update(range(tab, tab + 12))
        for race in range(6):
            sel = rom_w(tab + 2 * race)
            if not sel: continue
            r = tab + 2 * sel
            while s16(rom_w(r)) != -1: r += 10
            out.update(range(tab + 2 * sel, r + 2))
    return out


def edge_bitmap_bytes():
    """Per race: descriptor pointer tables (+0x2e, 36 longs; ultimate 4 tables of the
    scroll frames 0x40045c = frame * 0x90) and the 36 edge bitmaps of each. Column c of
    table k: bitmap at rom_l(desc + 0x2e + 0x90 * k + 4 * c), (maxd + 7) // 8 bytes (the
    last diagonal always has a bit, scans never leave the bitmap)."""
    out = set()
    for race in range(6):
        desc = rom_l(0x2be00 + 4 * race); maxd = rom_w(desc + 0x18)
        n = 4 if race == 5 else 1                        # ultimate: list 0x24e2a has 4 frames
        for k in range(n):
            t = desc + 0x2e + 0x90 * k
            out.update(range(t, t + 0x90))
            for c in range(36):
                b = rom_l(t + 4 * c)
                out.update(range(b, b + (maxd + 7) // 8))
    return out


def mask_entries():
    """Mask entry codes 0x264aa can emit (bit 15 = hflip variants not listed): the explicit
    occluder codes (not the z compare of control -2), the slope table 0x1ead2, the full
    mask. Code 0 (placeholder records) ends an entry list: never drawn."""
    ents = {0x6ce0}
    for c in range(36):
        tab = rom_l(0x1ea2a + 4 * c)
        for race in range(6):
            sel = rom_w(tab + 2 * race)
            if not sel: continue
            r = tab + 2 * sel
            while s16(rom_w(r)) != -1:
                if s16(rom_w(r + 4)) != -2 and rom_w(r + 8): ents.add(rom_w(r + 8))
                r += 10
    for i in range(17): ents.add(rom_w(0x1ead2 + 2 * i) & 0x7fff)
    return ents


def mask_tile_codes():
    """MO codes of the tiles the mask entries draw: entry code & 0x7ff + k, height
    ((code >> 11) & 7) + 1 tiles."""
    return sorted({(e & 0x7ff) + k for e in mask_entries() for k in range(((e >> 11) & 7) + 1)})


def patches():
    """Playfield patches of mode 1 slots: [(race, record address, column, row, width, height,
    block address)], one per animation record with a tile block (tag 0x19 rows excluded)."""
    out = []
    for race in range(6):
        ents, roots = scripts_of(race)
        patch = set()
        code, anims, tags = walk(roots, patch)
        for lst, col, row in sorted(patch):
            if lst == 0x24e2a: continue                  # ultimate: row index, not a block
            n = 0
            while rom_l(lst + 8 * n) != 0xffffffff:
                t = rom_l(lst + 8 * n)
                if t: out.append((race, lst + 8 * n, col, row, PROG[t], PROG[t + 1], t))
                n += 1
    return out


def main():
    verbose = "-v" in sys.argv
    tot = 0
    for race in range(6):
        ents, roots = scripts_of(race)
        code, anims, tags = walk(roots)
        lists, tiles, dyn, nmo, ntb, ndb = frames(anims)
        size = 6 * len(ents) + 2 + len(code) + lists + tiles + dyn
        tot += size
        print("race %d: %d spawn entries, script %d B, %d animation lists %d B, %d tile "
              "blocks %d B, %d terrain blocks %d B, %d MO frames, tags %s -> %d B"
              % (race, len(ents), len(code), len(anims), lists, ntb, tiles, ndb, dyn, nmo,
                 " ".join("%02x" % t for t in sorted(tags)), size))
        if verbose:
            for a, s, t, md_ in sorted(anims):
                print("   anim %06x stride %d tag %02x mode %d" % (a, s, t, md_))
    expl, bitmaps, small = occlusion_tables()
    print("objects total %d B; occlusion: explicit occluders %d B, edge bitmaps %d B, "
          "small tables ~%d B" % (tot, expl, bitmaps, small))


if __name__ == "__main__":
    main()
