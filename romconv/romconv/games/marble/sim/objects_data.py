"""Marble Madness level object data per race: what a remake port has to carry.

Walks the spawn lists (0x1dec0[race], entries lo band, hi band, long script; script 0 =
random group of 4 from 0x1ded8), every slot script reachable from them (byte code
0x12896, MARBLE.md "Level objects"), the animation lists the scripts select and the
frame records they hold, and the occlusion tables (0x264aa).

Every function reads the program ROM of its `rom` argument (marble_sim.SimRom).
"""
from types import MappingProxyType

from romconv.games.marble.sim.marble_sim import s16

_ARGS = MappingProxyType({0: None, 1: 4, 2: 4, 3: 2, 4: 2, 5: 0, 6: 2, 7: 0, 8: 6, 9: 4, 10: 4, 11: 0,
                         12: 4, 13: 4, 14: 4, 15: 0, 16: 0, 17: 2, 18: 4})


def scripts_of(rom, race):
    a = rom.l(0x1dec0 + 4 * race)
    ents = []
    while rom.prog[a] != 0xff:
        ents.append((rom.prog[a], rom.prog[a + 1], rom.l(a + 2))); a += 6
    roots = []
    for lo, hi, sc in ents:
        roots += [rom.l(0x1ded8 + 4 * k) for k in range(16)] if sc == 0 else [sc]
    if race == 2: roots.append(0x1d854)              # wave, spawned by 0x12fd0
    if race == 3:                                    # aerial scripts entered by pc (mode 0
        roots += [(0x1d766, 0, 0x0b, None), (0x1d7ac, 0, 0x0d, None),  # slots), with their
                  (0x1d752, 0, 0x0b, None), (0x1d798, 0, 0x0d, None),  # tag: gate wait
                  (0x1db80, 0, 0x0a, None)]          # (0x13966), close (0x13892, 0x2563e),
                                                     # catapult throw (0x29e22)
    return ents, roots


def walk(rom, roots, patch=None):
    """Script bytes, animation lists (pointer, frame stride), tags. patch: optional set,
    receives (list pointer, column, row) of mode 1 slots (op 0 and ops 12/13 lists)."""
    code, anims, tags, seen = set(), set(), set(), set()
    todo = [r if isinstance(r, tuple) else (r, 0, 0, None) for r in roots]  # (pc, mode, tag, row)
    while todo:
        pc, mode, tag, crow = todo.pop()
        loops = {}
        while (pc, mode) not in seen:
            seen.add((pc, mode))
            op = s16(rom.w(pc)); start = pc; pc += 2
            if op < 0 or op > 18: code.update(range(start, pc)); break
            if op == 0:
                mode = rom.prog[pc + 7]; tag = rom.prog[pc + 9]; tags.add(tag); pc += 10
                if mode == 1:
                    crow = (rom.w(pc), rom.w(pc + 2))
                    anims.add((rom.l(pc + 4), 8, tag, 1)); pc += 8
                    if patch is not None: patch.add((rom.l(pc - 4),) + crow)
            else:
                pc += _ARGS[op]
            code.update(range(start, pc))
            if op in (4, 6): loops[op] = rom.prog[pc - 1]
            if op in (5, 7) and loops.get(op - 1) == 0: break     # count 0: loops forever
            if op in (12, 13):
                anims.add((rom.l(pc - 4), 8 if mode else 4, tag, mode))
                if patch is not None and mode == 1: patch.add((rom.l(pc - 4),) + crow)
            if op in (8, 10, 18): todo.append((rom.l(pc - 4), mode, tag, crow))
            if op == 9: pc = rom.l(pc - 4); continue
            if op in (11, 15): break
            if op == 3 and rom.w(pc - 2) == 0: break
            if op in (1, 2) and rom.w(pc - 4) == 0: break
    return code, anims, tags


def object_parts(rom, race):
    """Program ROM bytes the object slots of one race read, as (script bytes, animation list
    bytes, terrain code blocks of mode 1/2 entries). Playfield tile blocks (never read by the
    sim, patched by the remake from `patches`) and MO frames (format 0x1a8d2, Phase 4) are not
    included."""
    _, roots = scripts_of(rom, race)
    code, anims, tags = walk(rom, roots)
    lists, dyn = set(), set()
    for lst, stride, tag, mode in anims:
        n = 0
        while rom.l(lst + n * stride) != 0xffffffff and n < 500: n += 1
        lists.update(range(lst, lst + n * stride + 4))
        if stride != 8: continue
        for k in range(n):
            d = rom.l(lst + k * stride + 4)
            if d: dyn.update(range(d, d + 4 + 2 * rom.w(d + 2)))
    return set(code), lists, dyn


def global_anim_bytes(rom):
    """The three global animation lists of tag 3 (0x40044a/e/52, wrap targets 0x211fe,
    0x2121e, 0x2126e, 0x212b2) with the long before each (0x13068 reads it)."""
    out = set()
    for a in (0x211fe, 0x2121e, 0x2126e, 0x212b2):
        e = a
        while rom.l(e) != 0xffffffff: e += 4
        out.update(range(a - 4, e + 4))
    return out


def occluder_bytes(rom):
    """Explicit occluder lists of 0x264aa (0x1ea2a): per column 0-35 a table of 6 word offsets
    (one per race, 0 = none) to records of 5 words (diagonal limit, depth, control, y, entry
    code) up to a limit word of -1. Lists overlap by whole words (a list may start inside the
    record of another), so the remake keeps them word addressed."""
    out = set()
    for c in range(36):
        tab = rom.l(0x1ea2a + 4 * c)
        out.update(range(tab, tab + 12))
        for race in range(6):
            sel = rom.w(tab + 2 * race)
            if not sel: continue
            r = tab + 2 * sel
            while s16(rom.w(r)) != -1: r += 10
            out.update(range(tab + 2 * sel, r + 2))
    return out


def mask_entries(rom):
    """Mask entry codes 0x264aa can emit (bit 15 = hflip variants not listed): the explicit
    occluder codes (not the z compare of control -2), the slope table 0x1ead2, the full
    mask. Code 0 (placeholder records) ends an entry list: never drawn."""
    ents = {0x6ce0}
    for c in range(36):
        tab = rom.l(0x1ea2a + 4 * c)
        for race in range(6):
            sel = rom.w(tab + 2 * race)
            if not sel: continue
            r = tab + 2 * sel
            while s16(rom.w(r)) != -1:
                if s16(rom.w(r + 4)) != -2 and rom.w(r + 8): ents.add(rom.w(r + 8))
                r += 10
    for i in range(17): ents.add(rom.w(0x1ead2 + 2 * i) & 0x7fff)
    return ents


def mask_tile_codes(rom):
    """MO codes of the tiles the mask entries draw: entry code & 0x7ff + k, height
    ((code >> 11) & 7) + 1 tiles."""
    return sorted({(e & 0x7ff) + k for e in mask_entries(rom) for k in range(((e >> 11) & 7) + 1)})


def patches(rom):
    """Playfield patches of mode 1 slots: [(race, record address, column, row, width, height,
    block address)], one per animation record with a tile block (tag 0x19 rows excluded)."""
    out = []
    for race in range(6):
        ents, roots = scripts_of(rom, race)
        patch = set()
        code, anims, tags = walk(rom, roots, patch)
        for lst, col, row in sorted(patch):
            if lst == 0x24e2a: continue                  # ultimate: row index, not a block
            n = 0
            while rom.l(lst + 8 * n) != 0xffffffff:
                t = rom.l(lst + 8 * n)
                if t: out.append((race, lst + 8 * n, col, row, rom.prog[t], rom.prog[t + 1], t))
                n += 1
    return out
