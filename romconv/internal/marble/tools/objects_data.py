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

from devrom import ROM, SIM_ROM, md  # noqa: F401
from romconv.games.marble.sim import objects_data as _model  # noqa: F401
from romconv.games.marble.sim.objects_data import *  # noqa: F401,F403
import marble_sim as ms
from marble_sim import PROG, rom_w, rom_l, s16

# The model takes its ROM explicitly (marble_sim.SimRom); the tools use the one loaded by devrom.
def scripts_of(race): return _model.scripts_of(SIM_ROM, race)
def walk(roots, patch=None): return _model.walk(SIM_ROM, roots, patch)
def object_bytes(race): return _model.object_bytes(SIM_ROM, race)
def object_tables(): return _model.object_tables(SIM_ROM)
def occlusion_bytes(): return _model.occlusion_bytes(SIM_ROM)
def mask_entries(): return _model.mask_entries(SIM_ROM)
def mask_tile_codes(): return _model.mask_tile_codes(SIM_ROM)
def patches(): return _model.patches(SIM_ROM)





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
