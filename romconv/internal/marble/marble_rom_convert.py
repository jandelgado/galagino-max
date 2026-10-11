#!/usr/bin/env python3
"""Marble Madness: per-level playfield tiles, maps and palettes for the galagino remake.

Each level gets its own tile set (only the tiles its map uses, in first-use order) so one
level fits fixed Arena buffers (MAX_TILES x 64 + MAX_ROWS x 36 x 2 bytes). Map word:
bits 0-9 local tile, 10-11 colour set, 15 hflip. Also writes the host test fixture.

Phase 2 (marble physics, MARBLE.md "Remake data"): terrain per race, the program ROM
blocks the physics reads, marble motion object tiles and palette.

Phase 3 (level objects and occlusion): more program ROM blocks (slot scripts, animation
lists, terrain blocks, occlusion and edge bitmap tables), practice gate playfield patches
(tiles added to the practice tile set), occlusion mask tiles.
"""
import os
import sys
import zlib
import struct

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "pyconv"))
import marble_data as md
from asset_emit import emit_compressed
from gfxutil import rgb888_to_rgb565_le
from convutil import fatal

sys.path.insert(0, os.path.join(md.HERE, "tools"))
import marble_sim as ms
import objects_data as od

ROOT = os.path.join(md.HERE, "..", "..", "..")
OUT = os.path.join(ROOT, "source", "src", "machines", "marble", "marble_rom.h")
FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_level", "marble_fixture.h")
TRACE_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_physics", "marble_trace.h")
OBJ_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_objects",
                           "marble_objects_trace.h")

MAX_ROWS = 145   # must match marble_level.h
MAX_TILES = 896  # must match marble_level.h
VIEW_W, VIEW_H, PAN = 240, 288, 48


PATCH_WORDS = {}   # (race, block address) -> map words (local tile | colour set << 10 | flip)


def convert_level(rom, n):
    words = md.decode_level(rom, n)
    local = {}
    tiles = []
    mapw = []

    def tile_word(w):
        code, cs, flip = md.pf_lookup(rom, w)
        if code not in local:
            local[code] = len(tiles)
            tiles.append(md.pf_tile(rom, code))
            if len(tiles) > 1024:
                fatal("marble: more than 1024 tiles in a level (map word has 10 tile bits)")
        if cs > 3:
            fatal(f"marble: colour set {cs} > 3 (map word has 2 colour bits)")
        return local[code] | cs << 10 | (0x8000 if flip else 0)

    for row in words:
        for w in row:
            mapw.append(tile_word(w))
    # playfield patches of the level objects: their tiles join the tile set
    for race, rec, col, prow, w, h, blk in od.patches():
        if race != n or (n, blk) in PATCH_WORDS: continue
        PATCH_WORDS[(n, blk)] = [tile_word(md.u16(rom, blk + 2 + 2 * k)) for k in range(w * h)]
        if len(tiles) > MAX_TILES:
            fatal(f"marble: race {n} patch tiles raise the tile set to {len(tiles)} > "
                  f"MAX_TILES {MAX_TILES}")
    rgb = md.palette(rom, n)[md.PF_PALETTE:md.PF_PALETTE + 128]
    pal = [rgb888_to_rgb565_le(*c) for c in rgb]
    return len(words), tiles, mapw, pal


def frame_crc(rows, tiles, mapw, pal, sx, sy):
    """Reference for marble_video::renderRow, 240x288 frame as little-endian uint16."""
    out = bytearray()
    for y in range(sy, sy + VIEW_H):
        for x in range(sx, sx + VIEW_W):
            w = mapw[(y >> 3) * md.COLS + (x >> 3)]
            c = x & 7
            if w & 0x8000:
                c = 7 - c
            p = tiles[w & 0x3FF][(y & 7) * 8 + c]
            out += struct.pack("<H", pal[((w >> 10) & 3) * 32 + p])
    return zlib.crc32(out)


# ---------------------------------------------------------------- Phase 2: marble physics

FLASH_BUDGET = 50 * 1024
PHASE3_BUDGET = 25 * 1024
PHASE3_BEFORE = 22777  # flash B of the Phase 3 data with edge bitmaps and the mask map
EDGE_TABLES = None
PAIR_BYTES = 54   # two diagonals, 44 x 10 bit fields MSB first, last 8 bits not stored
# Program ROM tables the physics, start, respawn and camera read (address, length). Read
# extents follow from the index ranges in marble_sim.py; the recorded reads below must fall
# inside these plus the descriptors, zone line and checkpoint lists.
PROG_TABLES = (
    (0x24b3a, 0x2a),  # 0x1caba vertex steps, even and odd diagonals (4 x 6 bytes each)
    (0x1ed62, 0x40),  # two level codes: drop per (code >> 7) & 0x1f
    (0x23d40, 0x10),  # 0x1706c edge push per cell fraction 0-7
    (0x2398c, 0x05),  # 0x160f6 teeter speeds 0-4
    (0x1eef8, 0x22),  # 0x25e7c friction, 17 words
    (0x1eb16, 0x24),  # zone line list per race, goal velocity x (0x1eb2e) and y (0x1eb34)
    (0x1ef1a, 0x18),  # checkpoint list per race (respawn 0x2637a)
    (0x1f1ca, 0x0e),  # camera 0x13ee6 thresholds and speeds
    (0x2be00, 0x18),  # level descriptor per race
    (0x1d370, 0x84),  # intermediate dynamic terrain codes at race start (0x11ac2)
    (0x1699c, 0x16),  # practice, beginner, ultimate dynamic codes: first animation frame
    (0x1b908, 0x1c),  # (0x13628: slot, count, words)
    (0x24e4e, 0xe4),
)
DESC_BYTES = 0x2e  # descriptor fields +0x00..+0x2d
MO_CODES = range(0x03, 0x2e)  # marble body 0x03-0x06, 0x07, surface dots 0x0d-0x2d


def terrain_race(rom_terrain):
    """Terrain of one race as emitted: index bytes, codes, records bytes, bases."""
    t = rom_terrain
    if t.maxd & 1:
        fatal(f"marble: race {t.race} has an odd diagonal count")
    # pair p as written to video RAM 0x1eb3a[p] (validated against MAME: marble_sim.py terrain)
    img = t.vram_image()
    index = b"".join(img[ms.s16(ms.rom_w(0x1eb3a + 2 * p))] for p in range(t.maxd // 2))
    rd = ms.Rd(t.race)
    rd.bank = t.bank
    recs = rd.l(t.desc)
    end = recs
    while rd.l(end) != 0xFFFFFFFF:
        end += 4
    records = bytes(rd.b(a) for a in range(recs, end + 4))
    used = {t.codes[i] for d in t.diag for i in d}
    for c in used:
        if 0 < c < 0x800 and c + 4 > len(records):
            fatal(f"marble: race {t.race} record code {c:04x} beyond its records")
    check_dynamic(t, used, len(records))
    return bytes(index), t.codes, records, recs, t.bases


def check_dynamic(t, used, nrecords):
    """Dynamic codes 0x0800-0x0fff the terrain uses: slot set by dyn_init, value a record
    code within the records or a non-dynamic code (0 = not initialised: no floor)."""
    m = ms.Mem(t, bytes(0x2000))
    ms.dyn_init(m, t.race)
    for c in sorted(c for c in used if 0x800 <= c < 0x1000):
        v = m.rw(ms.DYN + (c & 0x7FE))
        if v == 0:
            fatal(f"marble: race {t.race} dynamic code {c:04x} not initialised by dyn_init")
        if 0x800 <= v < 0x1000:
            fatal(f"marble: race {t.race} dynamic code {c:04x} maps to dynamic code {v:04x}")
        if v < 0x800 and v + 4 > nrecords:
            fatal(f"marble: race {t.race} dynamic code {c:04x} -> record {v:04x} beyond records")


class RecProg(bytearray):
    """Program ROM that records the byte addresses read (indexing and slices)."""
    reads = set()
    def __getitem__(self, k):
        if isinstance(k, slice):
            RecProg.reads.update(range(*k.indices(len(self))))
        else:
            RecProg.reads.add(k)
        return bytearray.__getitem__(self, k)


class BlockProg(bytearray):
    """Program ROM that serves only the emitted blocks: a read outside fails."""
    def __init__(self, blocks):
        super().__init__(0x30000)
        self.cov = bytearray(0x30000)
        for addr, data in blocks:
            self[addr:addr + len(data)] = data
            self.cov[addr:addr + len(data)] = b"\x01" * len(data)
    def __getitem__(self, k):
        if isinstance(k, slice):
            span = range(*k.indices(len(self)))
            if any(not self.cov[a] for a in span):
                fatal(f"marble: ROM read {span.start:06x}+{len(span)} outside the emitted blocks")
        elif not self.cov[k]:
            fatal(f"marble: ROM read {k:06x} outside the emitted blocks")
        return bytearray.__getitem__(self, k)


class RecMem(ms.Mem):
    """Mem that records the slapstic window addresses the physics reads."""
    slap = {}  # race -> 68010 addresses in the slapstic window
    def rb(self, a):
        a &= 0xFFFFFF
        if 0x80000 <= a < 0x88000:
            RecMem.slap.setdefault(self.t.race, set()).add(a)
        return super().rb(a)


class BlockMem(ms.Mem):
    """Mem that serves slapstic data only from the emitted terrain records; program ROM
    comes from BlockProg (ms.PROG replaced while it runs)."""
    records = {}
    def rb(self, a):
        a &= 0xFFFFFF
        if 0x80000 <= a < 0x88000:
            base, data = BlockMem.records[self.t.race]
            if not base <= a < base + len(data):
                fatal(f"marble: ROM read {a:06x} outside the emitted blocks")
            return data[a - base]
        return super().rb(a)


def physics_runs(mem, terrains, objects=False):
    """Race starts of all races and every Phase 2 fixture tick, on Mem class mem (with
    objects: slots, projection and masks on). Returns positions."""
    out = []
    for n in range(md.LEVELS):
        m = mem(terrains[n], bytes(0x2000))
        m.objects = m.sprites = objects
        ms.start_259b4(m, n)
        out.append(tuple(m.sl(ms.P + o) for o in (0xC, 0x10, 0x14)))
    for seg in ms.load_fixture(TRACE_FIXTURE):
        m = ms.fixture_mem(seg, terrains[seg["race"]], mem)
        m.objects = m.sprites = objects
        if ms.replay_fixture(seg, m) is not None:
            fatal("marble: fixture replay differs, run marble_sim.py fixture")
        out.append(tuple(m.sl(ms.P + o) for o in (0xC, 0x10, 0x14)))
    return out


OBJ_IDLE_TICKS = 200   # past the practice gate wait (150 ticks) and its first frames


def object_runs(mem, terrains):
    """Phase 3: level init (objects_init) of all races plus idle ticks on a cleared RAM, and
    every object fixture segment, objects and sprites on. Returns RAM crc32 per run."""
    out = []
    for n in range(md.LEVELS):
        m = mem(terrains[n], bytes(0x2000))
        m.objects = m.sprites = True
        ms.start_259b4(m, n)
        ms.objects_init(m, n)
        for _ in range(OBJ_IDLE_TICKS): ms.tick(m, 0, 0)
        out.append(zlib.crc32(bytes(m.ram)))
    if not os.path.exists(OBJ_FIXTURE):
        fatal(f"marble: {OBJ_FIXTURE} missing, run tools/make_fixture.py owrite")
    for seg in ms.load_objfixture(OBJ_FIXTURE):
        m = ms.fixture_mem(seg, terrains[seg["race"]], mem)
        if ms.replay_objfixture(seg, m) is not None:
            fatal("marble: object fixture replay differs, run marble_sim.py objfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def merge(addrs, gap=8):
    """Sorted byte addresses -> [first, end) ranges, joining gaps up to gap bytes."""
    out = []
    for a in sorted(addrs):
        if out and a <= out[-1][1] + gap:
            out[-1][1] = max(out[-1][1], a + 1)
        else:
            out.append([a, a + 1])
    return out


def table_bytes(descs):
    """Phase 2 tables (whole) as a byte address set."""
    tables = list(PROG_TABLES) + [(d, DESC_BYTES) for d in descs]
    for n in range(md.LEVELS):
        for table, entry, last in ((0x1eb16, 6, lambda b: b[3] == 0xFF and b[4] == 0xFF),
                                   (0x1ef1a, 4, lambda b: b[0] == 0xFF)):
            a = ms.rom_l(table + 4 * n)
            start = a
            while not last(ms.PROG[a:a + entry]):
                a += entry
            tables.append((start, a + entry - start))
    covered = set()
    for a, length in tables:
        covered |= set(range(a, a + length))
    return covered


def phase3_bytes():
    """Phase 3 tables (whole): level objects, occlusion (edge bitmaps are edge lists)."""
    out = set(od.object_tables()) | od.occlusion_bytes()
    for race in range(md.LEVELS):
        out |= od.object_bytes(race)
    return out


def edge_tables():
    """Terrain edge diagonals of 0x264aa. Per race and descriptor table (4 for the ultimate
    race, one per scroll frame) and column 0-35: the ascending diagonals < maxd with a bit in
    the edge bitmap at descriptor +0x2e. Returns [(race, [36 lists])] per table, checks that
    the list lookup equals the bitmap scan (with the remake guard) for every d < maxd."""
    out = []
    for race in range(md.LEVELS):
        desc = ms.rom_l(0x2be00 + 4 * race)
        maxd = ms.rom_w(desc + 0x18)
        for k in range(4 if race == 5 else 1):
            cols = []
            for c in range(36):
                bm = ms.rom_l(desc + 0x2e + 0x90 * k + 4 * c)
                lst = [d for d in range(maxd) if ms.PROG[bm + (d >> 3)] >> (d & 7) & 1]
                if not lst or lst[-1] != maxd - 1 or maxd > 256:
                    fatal(f"marble: race {race} column {c}: edge list unusable")
                cols.append(lst)
            out.append((race, cols))
    return out


def edge_lookup(tables, race, ult, c, d):
    """First edge diagonal >= d of column c (-1: none), as the remake reads the lists."""
    base = sum(1 for r, _ in tables if r < race)
    for e in tables[base + ult // 0x90][1][c]:
        if e >= d: return e
    return -1


def edge_check(tables):
    """List lookup == arcade bitmap scan (0x264aa, first set bit at or after d) for all d."""
    for race in range(md.LEVELS):
        desc = ms.rom_l(0x2be00 + 4 * race)
        maxd = ms.rom_w(desc + 0x18)
        for k in range(4 if race == 5 else 1):
            for c in range(36):
                bm = ms.rom_l(desc + 0x2e + 0x90 * k + 4 * c)
                for d in range(maxd + 1):
                    want = next((e for e in range(d, maxd)
                                 if ms.PROG[bm + (e >> 3)] >> (e & 7) & 1), -1)
                    if edge_lookup(tables, race, 0x90 * k, c, d) != want:
                        fatal(f"marble: edge list differs from bitmap: race {race} "
                              f"table {k} column {c} d {d}")


def prog_blocks(terrains):
    """Program ROM ranges the sim reads. Returns (blocks, phase 2 blocks, reference runs):
    phase 2 = physics reads plus the Phase 2 tables, all = phase 2 plus everything the
    object and occlusion runs read plus the Phase 3 tables."""
    covered = table_bytes([t.desc for t in terrains])
    phase3 = phase3_bytes()      # before ms.PROG is replaced
    ms.EDGES = lambda race, ult, c, d: edge_lookup(EDGE_TABLES, race, ult, c, d)
    plain = ms.PROG
    ms.PROG = RecProg(plain)
    try:
        RecProg.reads.clear(); RecMem.slap.clear()
        ref2 = physics_runs(RecMem, terrains)
        want = set(RecProg.reads)
        ref3 = physics_runs(RecMem, terrains, True) + object_runs(RecMem, terrains)
        want3 = set(RecProg.reads)
    finally:
        ms.PROG = plain
        ms.EDGES = None
    outside = sorted(want - covered)
    if outside:
        fatal("marble: physics reads ROM outside the known tables: "
              + ", ".join(f"{a:06x}" for a in outside[:16]))
    p2 = [(a, bytes(plain[a:e])) for a, e in merge(want | covered)]
    allb = [(a, bytes(plain[a:e])) for a, e in merge(want3 | covered | phase3)]
    dyn_only = want3 - covered - phase3
    if dyn_only:
        print(f"phase 3 runs read {len(dyn_only)} B outside the tables: "
              + ", ".join(f"{a:05x}+{e - a}" for a, e in merge(dyn_only, 0)))
    return allb, p2, ref2 + ref3


def mo_sprites(rom):
    """Marble motion object tiles (codes MO_CODES, consecutive) and the 16 colour palette."""
    tiles = []
    sets = set()
    for code in MO_CODES:
        tile, cs = md.mo_lookup(rom, code)
        tiles.append(md.mo_tile(rom, tile))
        sets.add(cs)
    if len(sets) != 1:
        fatal("marble: marble motion objects use more than one palette set")
    cs = sets.pop()
    pals = {tuple(md.palette(rom, n)[0x100 + cs * 16:0x110 + cs * 16]) for n in range(md.LEVELS)}
    if len(pals) != 1:
        fatal("marble: motion object palette differs between races")
    return tiles, [rgb888_to_rgb565_le(*c) for c in pals.pop()]


def c_bytes(b, indent="  "):
    return ",\n".join(indent + ",".join(f"0x{v:02x}" for v in b[k:k + 24])
                      for k in range(0, len(b), 24))


def c_words(w, indent="  "):
    return ",\n".join(indent + ",".join(f"0x{v:04x}" for v in w[k:k + 16])
                      for k in range(0, len(w), 16))


def emit_physics(f, rom, size3):
    """Phase 2 data into marble_rom.h. Returns {item: flash bytes}; the prog blocks added by
    Phase 3 go to size3."""
    size = {}
    global EDGE_TABLES
    terrains = [ms.Terrain(n) for n in range(md.LEVELS)]
    races = [terrain_race(t) for t in terrains]
    EDGE_TABLES = edge_tables()
    edge_check(EDGE_TABLES)
    print("\n// ---- Phase 2: marble physics (MARBLE.md \"Remake data\")", file=f)
    print("// Terrain per race: 10 bit vertex index (54 bytes per diagonal pair, fields MSB first,",
          file=f)
    print("// field = column, +22 on odd diagonals, field 43 not stored = 0) into codes[];", file=f)
    print("// record codes 0x0001-0x07ff read records[code + quadrant]; bases[] per diagonal.",
          file=f)
    print("struct MarbleTerrainInfo {\n  const uint8_t *index; uint16_t pairs;\n"
          "  const uint16_t *codes; uint16_t ncodes;\n  const uint8_t *records;\n"
          "  const uint16_t *bases; uint16_t maxd;\n  uint16_t nrecords; uint32_t desc;\n"
          "  const uint16_t *edges; uint8_t nedges;\n};",
          file=f)
    # edge lists: marble_edge_lists[marble_edge_off[col]...] of the table of race and scroll
    # frame, first diagonal >= d (the last list entry is maxd - 1); equal lists are shared
    pool, where, offs = bytearray(), {}, {}
    for t, (race, cols) in enumerate(EDGE_TABLES):
        row = []
        for lst in cols:
            key = bytes(lst)
            if key not in where:
                where[key] = len(pool); pool += key
            row.append(where[key])
        offs.setdefault(race, []).append(row)
    if len(pool) > 0xffff:
        fatal("marble: edge lists too long for 16 bit offsets")
    print("// Terrain edge diagonals (0x264aa): column c of scroll frame k (the ultimate race has 4,",
          file=f)
    print("// others 1) of a race: ascending diagonals < maxd from marble_edge_lists[edges[36 * k +",
          file=f)
    print("// c]]; the first entry >= d is the edge, the last is maxd - 1.", file=f)
    print(f"static const uint8_t marble_edge_lists[{len(pool)}] = {{\n{c_bytes(pool)}\n}};",
          file=f)
    for race, rows in sorted(offs.items()):
        flat = [o for row in rows for o in row]
        print(f"static const uint16_t marble_edge_off_{race}[{len(flat)}] = {{\n{c_words(flat)}\n}};",
              file=f)
    size3["edge lists"] = len(pool) + 2 * sum(len(r) * 36 for r in offs.values()) + 6 * 8
    code_names = {}
    for n, (index, codes, records, _, bases) in enumerate(races):
        print(f"static const uint8_t marble_terrain_index_{n}[] = {{\n{c_bytes(index)}\n}};",
              file=f)
        size["terrain index"] = size.get("terrain index", 0) + len(index)
        key = tuple(codes)
        if key not in code_names:
            code_names[key] = f"marble_terrain_codes_{n}"
            print(f"static const uint16_t {code_names[key]}[] = {{\n{c_words(codes)}\n}};", file=f)
            size["terrain codes"] = size.get("terrain codes", 0) + 2 * len(codes)
        print(f"static const uint8_t marble_terrain_records_{n}[] = {{\n{c_bytes(records)}\n}};",
              file=f)
        print(f"static const uint16_t marble_terrain_bases_{n}[] = {{\n{c_words(bases)}\n}};",
              file=f)
        size["terrain records"] = size.get("terrain records", 0) + len(records)
        size["terrain bases"] = size.get("terrain bases", 0) + 2 * len(bases)
    print("static const MarbleTerrainInfo marble_terrain[6] = {", file=f)
    for n, (index, codes, records, _, bases) in enumerate(races):
        t = terrains[n]
        print(f"  {{ marble_terrain_index_{n}, {len(index) // PAIR_BYTES}, "
              f"{code_names[tuple(codes)]}, {len(codes)}, marble_terrain_records_{n}, "
              f"marble_terrain_bases_{n}, {t.maxd}, {len(records)}, 0x{t.desc:05x}, "
              f"marble_edge_off_{n}, {len(offs[n])} }},", file=f)
    print("};", file=f)
    size["terrain info"] = 6 * 32  # 32 bit pointers, padded

    BlockMem.records = {n: (r[3], r[2]) for n, r in enumerate(races)}
    blocks, blocks2, ref = prog_blocks(terrains)
    for n in range(md.LEVELS):
        recs, data = BlockMem.records[n]
        if any(not recs <= a < recs + len(data) for a in RecMem.slap.get(n, ())):
            fatal(f"marble: race {n} reads slapstic data outside its terrain records")
    plain = ms.PROG
    ms.PROG = BlockProg(blocks)
    ms.EDGES = lambda race, ult, c, d: edge_lookup(EDGE_TABLES, race, ult, c, d)
    try:
        got = (physics_runs(BlockMem, terrains) + physics_runs(BlockMem, terrains, True)
               + object_runs(BlockMem, terrains))
    finally:
        ms.PROG = plain
        ms.EDGES = None
    if got != ref:
        fatal("marble: simulation on the emitted blocks differs from the full ROM")
    print("// Program ROM bytes the physics, level objects and occlusion read (68010 address,",
          file=f)
    print("// sorted). Records and terrain come from marble_terrain; any other ROM address is a",
          file=f)
    print("// porting error.", file=f)
    print("struct MarbleProgBlock { uint32_t addr; uint16_t len; const uint8_t *data; };", file=f)
    for k, (a, data) in enumerate(blocks):
        print(f"static const uint8_t marble_prog_{a:05x}[] = {{\n{c_bytes(data)}\n}};", file=f)
    print(f"static const MarbleProgBlock marble_prog_blocks[{len(blocks)}] = {{", file=f)
    for a, data in blocks:
        print(f"  {{ 0x{a:05x}, {len(data)}, marble_prog_{a:05x} }},", file=f)
    print(f"}};\nstatic const uint8_t MARBLE_PROG_BLOCKS = {len(blocks)};", file=f)
    flash = lambda bl: sum(len(d) for _, d in bl) + 8 * len(bl)
    size["prog blocks"] = flash(blocks2)
    size3["prog blocks"] = flash(blocks) - flash(blocks2)

    tiles, pal = mo_sprites(rom)
    first = [0xFF] * MO_CODES.stop
    for k, code in enumerate(MO_CODES):
        first[code] = k
    print("// Marble motion objects: tile k of an MO of code c and height h (k < h) is", file=f)
    print("// marble_mo_tiles[marble_mo_code_first[c] + k] (0xff: not emitted); pen 0", file=f)
    print("// transparent, colour marble_mo_palette[pen] (palette RAM 0x100 + set * 16).", file=f)
    print(f"static const uint8_t MARBLE_MO_CODES = 0x{MO_CODES.stop:02x};", file=f)
    print("static const uint8_t marble_mo_code_first[MARBLE_MO_CODES] = {\n"
          + c_bytes(first) + "\n};", file=f)
    print(f"static const uint8_t marble_mo_tiles[{len(tiles)}][64] = {{", file=f)
    for t in tiles:
        print("  {" + ",".join(str(p) for p in t) + "},", file=f)
    print("};", file=f)
    print("static const uint16_t marble_mo_palette[16] = {\n" + c_words(pal) + "\n};",
          file=f)
    size["mo tiles"] = 64 * len(tiles) + len(first)
    size["mo palette"] = 32

    for n, (index, codes, records, _, bases) in enumerate(races):
        print(f"terrain {n} {md.NAMES[n]}: {terrains[n].maxd} diagonals, index {len(index)} B, "
              f"{len(codes)} codes, records {len(records)} B, bases {2 * len(bases)} B")
    print(f"prog blocks: {len(blocks)}, " + ", ".join(f"{a:05x}+{len(d)}" for a, d in blocks))
    return size


def emit_objects(f, rom, levels, size3):
    """Phase 3 data besides the prog blocks: playfield patches of the level objects and the
    occlusion mask tiles. Sizes into size3."""
    patches = od.patches()
    print("\n// ---- Phase 3: level objects and occlusion (MARBLE.md \"Remake data\")", file=f)
    print("// Playfield patches of mode 1 level objects (practice start gate): when the animation",
          file=f)
    print("// record at `record` (68010 address, 0x400970 queue) is applied, the w x h map words",
          file=f)
    print("// (same format as the level map) are written at map column `col`, race row `row` of",
          file=f)
    print("// race `race`.", file=f)
    print("struct MarblePatch {\n  uint32_t record; uint8_t race, col, row, w, h;\n"
          "  const uint16_t *words;\n};", file=f)
    for race, rec, col, row, w, h, blk in patches:
        if max(race, col, row, w, h) > 255:
            fatal(f"marble: patch {rec:05x} does not fit the byte fields")
    blocks = sorted(PATCH_WORDS)
    for race, blk in blocks:
        print(f"static const uint16_t marble_patch_words_{blk:05x}[] = {{\n"
              f"{c_words(PATCH_WORDS[(race, blk)])}\n}};", file=f)
    print(f"static const MarblePatch marble_patches[{len(patches)}] = {{", file=f)
    for race, rec, col, row, w, h, blk in patches:
        print(f"  {{ 0x{rec:05x}, {race}, {col}, {row}, {w}, {h}, marble_patch_words_{blk:05x} }},",
              file=f)
    print(f"}};\nstatic const uint8_t MARBLE_PATCHES = {len(patches)};", file=f)
    size3["patches"] = 16 * len(patches) + sum(2 * len(w) for w in PATCH_WORDS.values())
    # the closed state of a patched race shows the map as decoded: per race, one record
    # (the first frame of the gate) must equal the decoded map words
    for race in sorted({p[0] for p in patches}):
        mapw = levels[race][2]
        if not any(all(mapw[(row + r) * md.COLS + col + c] == PATCH_WORDS[(rc, blk)][r * w + c]
                       for r in range(h) for c in range(w))
                   for rc, rec, col, row, w, h, blk in patches if rc == race):
            fatal(f"marble: race {race}: no patch frame equals the decoded level map, "
                  "patch words are wrong")

    codes = od.mask_tile_codes()
    tiles = []
    for code in codes:
        tile, _ = md.mo_lookup(rom, code)
        px = md.mo_tile(rom, tile)
        if max(px) > 1:
            fatal(f"marble: mask tile {code:03x} uses pen {max(px)} (only pens 0/1 are masks)")
        tiles.append([sum((px[8 * y + x] & 1) << (7 - x) for x in range(8)) for y in range(8)])
    first, last = codes[0], codes[-1]
    by = dict(zip(codes, tiles))
    dense = [by.get(c, [0] * 8) for c in range(first, last + 1)]   # absent code: blank tile
    print("// Occlusion mask tiles: MO tile of code c (marble_mask_index(c), 0xffff: none) is 8",
          file=f)
    print("// rows, bit 7 = left pixel, 1 = mask pixel (pen 1; pen 0 transparent). A mask entry",
          file=f)
    print("// of MO code c and height h uses codes c .. c + h - 1. Codes in between that no entry",
          file=f)
    print("// uses are blank tiles (no mask pixel).", file=f)
    print(f"static const uint16_t MARBLE_MASK_FIRST = 0x{first:03x};", file=f)
    print(f"static const uint16_t MARBLE_MASK_CODES = {len(dense)};", file=f)
    print(f"static const uint8_t marble_mask_tiles[{len(dense)}][8] = {{", file=f)
    for t in dense:
        print("  {" + ",".join(f"0x{v:02x}" for v in t) + "},", file=f)
    print("};", file=f)
    print("static inline uint16_t marble_mask_index(uint16_t code) {\n"
          "  return (uint16_t)(code - MARBLE_MASK_FIRST) < MARBLE_MASK_CODES\n"
          "      ? (uint16_t)(code - MARBLE_MASK_FIRST) : 0xffff;\n}", file=f)
    size3["mask tiles"] = 8 * len(dense)
    print(f"patches: {len(patches)} records, {len(PATCH_WORDS)} blocks; mask tiles: {len(tiles)} "
          f"of codes 0x{first:03x}-0x{last:03x}")


def main():
    rom = md.load()
    levels = [convert_level(rom, n) for n in range(md.LEVELS)]
    if max(len(l[1]) for l in levels) > MAX_TILES:
        fatal(f"marble: a tile set exceeds MAX_TILES {MAX_TILES}")
    if max(l[0] for l in levels) != MAX_ROWS or max(len(l[1]) for l in levels) != MAX_TILES:
        fatal("MAX_ROWS / MAX_TILES changed, update marble_level.h and this file")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        print("// Generated by romconv/internal/marble/marble_rom_convert.py; do not edit.", file=f)
        print("#ifndef MARBLE_ROM_H\n#define MARBLE_ROM_H\n#include <stdint.h>", file=f)
        print("struct MarbleLevelInfo { uint16_t rows; uint16_t tiles; };", file=f)
        print("static const MarbleLevelInfo marble_levels[6] = {", file=f)
        print("  " + ", ".join(f"{{ {r}, {len(t)} }}" for r, t, _, _ in levels) + "\n};", file=f)
        for n, (rows, tiles, mapw, _) in enumerate(levels):
            emit_compressed(f, f"marble_l{n}_map", "unsigned short", "", len(mapw), mapw)
            emit_compressed(f, f"marble_l{n}_tiles", "unsigned char", "[64]", len(tiles),
                            [p for t in tiles for p in t])
        print("static const FlashAsset<unsigned short, COMPRESSED> marble_maps[6] = { "
              + ", ".join(f"marble_l{n}_map_blob" for n in range(md.LEVELS)) + " };", file=f)
        print("static const FlashAsset<unsigned char[64], COMPRESSED> marble_tiles[6] = { "
              + ", ".join(f"marble_l{n}_tiles_blob" for n in range(md.LEVELS)) + " };", file=f)
        print("static const unsigned short marble_pf_palette[6][128] = {", file=f)
        for _, _, _, pal in levels:
            print("  { " + ", ".join(f"0x{v:04x}" for v in pal) + " },", file=f)
        print("};", file=f)
        size3 = {}
        size = emit_physics(f, rom, size3)
        emit_objects(f, rom, levels, size3)
        print("#endif", file=f)
    total = sum(size.values())
    print("phase 2 flash: " + ", ".join(f"{k} {v}" for k, v in size.items()) + f", total {total} B")
    if total > FLASH_BUDGET:
        fatal(f"marble: phase 2 data {total} B over the {FLASH_BUDGET} B budget")
    total3 = sum(size3.values())
    print("phase 3 flash: " + ", ".join(f"{k} {v}" for k, v in size3.items())
          + f", total {total3} B (before {PHASE3_BEFORE} B, {PHASE3_BEFORE - total3} B less)")
    if total3 > PHASE3_BUDGET:
        fatal(f"marble: phase 3 data {total3} B over the {PHASE3_BUDGET} B budget")
    os.makedirs(os.path.dirname(FIXTURE), exist_ok=True)
    with open(FIXTURE, "w") as f:
        print("// Generated by romconv/internal/marble/marble_rom_convert.py; do not edit.", file=f)
        print("// Counts and CRC32s only, no ROM data.", file=f)
        print("static const uint16_t MARBLE_FIX_ROWS[6] = { "
              + ", ".join(str(l[0]) for l in levels) + " };", file=f)
        print("static const uint16_t MARBLE_FIX_TILES[6] = { "
              + ", ".join(str(len(l[1])) for l in levels) + " };", file=f)
        crcs = [zlib.crc32(struct.pack(f"<{len(l[2])}H", *l[2])) for l in levels]
        print("static const uint32_t MARBLE_FIX_MAP_CRC[6] = { "
              + ", ".join(f"0x{c:08x}u" for c in crcs) + " };", file=f)
        print("static const uint32_t MARBLE_FIX_FRAME_CRC[6][3] = {", file=f)
        for rows, tiles, mapw, pal in levels:
            c = [frame_crc(rows, tiles, mapw, pal, sx, 0) for sx in (0, PAN // 2, PAN)]
            print("  { " + ", ".join(f"0x{v:08x}u" for v in c) + " },", file=f)
        print("};", file=f)
    for n, (rows, tiles, _, _) in enumerate(levels):
        print(f"level {n} {md.NAMES[n]}: {rows} rows, {len(tiles)} tiles")


if __name__ == "__main__":
    main()
