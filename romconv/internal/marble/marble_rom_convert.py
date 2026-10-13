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

Phase 4a (level object sprites) and 4b-0 (player death sprites, shared by every race; their
tiles open the sprite tile buffer, the race set's follow).
"""
import os
import sys
import zlib
import struct

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "pyconv"))
import marble_data as md
from asset_emit import emit_compressed, lzma_compress
from gfxutil import rgb888_to_rgb565_le
from convutil import fatal

sys.path.insert(0, os.path.join(md.HERE, "tools"))
import marble_sim as ms
import marble_mo as mo
import marble_enemy as me
import marble_slinky as sl
import marble_acid as ac
import marble_bird as bd
import marble_mini as mn
import marble_piston as pp
import objects_data as od
import marble_hud as hud
import marble_attract as att
import marble_sid as msid

ROOT = os.path.join(md.HERE, "..", "..", "..")
OUT = os.path.join(ROOT, "source", "src", "machines", "marble", "marble_rom.h")
SOUND_ZIP = os.path.join(ROOT, "romszip", "marble_sound.zip")
SID_FILE = os.path.join(ROOT, "romszip", "Marble_Madness.sid")
SOUND_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_sound", "marble_sound_fixture.h")
SOUND_BUDGET = 10240
FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_level", "marble_fixture.h")
TRACE_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_physics", "marble_trace.h")
OBJ_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_objects",
                           "marble_objects_trace.h")
SPRITE_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_sprites",
                              "marble_sprites_trace.h")
STEELIE_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_steelies",
                               "marble_steelies_trace.h")
ACID_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_acid", "marble_acid_trace.h")
BIRD_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_birds", "marble_birds_trace.h")
TUBE_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_deaths", "marble_tubes_trace.h")
AERIAL_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_aerial",
                              "marble_aerial_trace.h")
MINI_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_minis", "marble_minis_trace.h")
PISTON_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_pistons",
                              "marble_pistons_trace.h")
SLINKY_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_slinkies",
                              "marble_slinkies_trace.h")
HUD_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_hud", "marble_hud_fixture.h")
ATTRACT_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_attract",
                               "marble_attract_fixture.h")
DEATH_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_deaths",
                             "marble_deaths_trace.h")

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
    rgb = md.palette(rom, n, True)[md.PF_PALETTE:md.PF_PALETTE + 128]
    pal = [rgb888_to_rgb565_le(*c) for c in rgb]
    return len(words), tiles, mapw, pal


TITLE_ROWS = 36   # title playfield: one screen, logo at rows 5-10 (MARBLE.md "Attract")
TITLE_TOP, TITLE_LEFT = 5, 8


def convert_title(rom):
    """Title playfield as a level: the logo words (marble_attract.LOGO) in a black map; ROM
    tiles, playfield set 0 = the logo palette."""
    local, tiles, mapw = {}, [], []

    def tile_word(w):
        code, cs, flip = md.pf_lookup(rom, w)
        if cs:
            fatal(f"marble: title word {w:04x} uses colour set {cs}")
        if code not in local:
            local[code] = len(tiles)
            tiles.append(md.pf_tile(rom, code))
        return local[code] | (0x8000 if flip else 0)

    blank = tile_word(0)
    if any(tiles[blank & 0x3ff]):
        fatal("marble: playfield word 0 is not a blank tile")
    for row in range(TITLE_ROWS):
        for col in range(md.COLS):
            y, x = row - TITLE_TOP, col - TITLE_LEFT
            inside = 0 <= y < len(att.LOGO) and 0 <= x < len(att.LOGO[0])
            mapw.append(tile_word(att.LOGO[y][x]) if inside else blank)
    rgb = [md.irgb_hw(w) for w in att.logo_palette(rom)]
    pal = [rgb888_to_rgb565_le(*c) for c in rgb] + [0] * 96
    return TITLE_ROWS, tiles, mapw, pal


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
    (0x1d364, 0x08),  # attract demo 0x1815a: waypoint list per demo race (practice, beginner)
    (0x2419e, 0x88),  # the two waypoint lists, 0 terminated
    (0x242aa, 0x1c),  # banner string chain per banner (BIOS 0x12a message)
)
DESC_BYTES = 0x2e  # descriptor fields +0x00..+0x2d
MO_CODES = range(0x01, 0x2e)  # particle half 0x01-0x02, marble body 0x03-0x06, 0x07, dots 0x0d-0x2d


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
        ms.snapshot_start(m)
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
        ms.snapshot_start(m)
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


SPRITES = []   # marble_mo.Sprites per race, built from the full ROM before it is replaced
SWEEP_MAX = [0] * md.LEVELS   # largest published object count of the race sweep, per race


def sprite_runs(mem, terrains):
    """Phase 4a: every sprite fixture segment (objects, draw list, sort, publish) on Mem class
    mem. Returns crc32 of the published lists per segment."""
    if not os.path.exists(SPRITE_FIXTURE):
        fatal(f"marble: {SPRITE_FIXTURE} missing, run tools/make_fixture.py swrite")
    out = []
    for seg in mo.load_spritefixture(SPRITE_FIXTURE):
        m = ms.fixture_mem(seg, terrains[seg["race"]], mem)
        bad, got = mo.replay_spritefixture(seg, m, SPRITES[seg["race"]])
        if bad is not None:
            fatal("marble: sprite fixture replay differs, run marble_mo.py spritefixture")
        out.append(zlib.crc32(repr(got).encode()))
    # race start and a camera sweep over the whole race with the draw list: every frame header
    # the sort reads, and the object counts the host test expects
    for n in range(md.LEVELS):
        m = mem(terrains[n], bytes(0x2000))
        m.objects = m.sprites = True
        m.drawlist = mo.DrawList()
        ms.start_race(m, n)
        ms.snapshot_start(m)
        SWEEP_MAX[n], got = mo.race_sweep(m, n, SPRITES[n])
        out.append(zlib.crc32(repr(got).encode()))
    return out


DEATH = None   # marble_mo.DeathSprites, built from the full ROM before it is replaced


def death_headers(m):
    """Frame headers the remake draws for the player (as ms.death_state, read from ms.PROG):
    rl(rl(+0x5a)) in states 1/2/5/9, the overlay rl(rl(+0x62)) when +0x67, else 0. Fails on a
    header outside the death set."""
    drawn = m.rb(ms.P + 0x1c)
    hdr = m.rl(m.rl(ms.P + 0x5a)) if drawn and m.rb(ms.P + 0x1a) in (1, 2, 5, 9) else 0
    shard = m.rl(m.rl(ms.P + 0x62)) if drawn and m.rb(ms.P + 0x67) else 0
    for h in (hdr, shard):
        if h and h not in DEATH.ids:
            fatal(f"marble: player frame {h:05x} not in the death set")
    return hdr, shard


def death_runs(mem, terrains, full):
    """Phase 4b-0: every death fixture segment (main tick with deaths, objects, sprites) on Mem
    class mem. The check values read the program ROM full (the player MO model reads frame
    bytes the remake replaces by the death set). Returns crc32 of the work RAM per segment."""
    if not os.path.exists(DEATH_FIXTURE):
        fatal(f"marble: {DEATH_FIXTURE} missing, run tools/make_fixture.py dwrite")
    out = []
    for seg in ms.load_dfixture(DEATH_FIXTURE):
        m = ms.fixture_mem(seg, terrains[seg["race"]], mem)
        m.deaths = m.objects = m.sprites = True
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            hdrs = death_headers(m)
            prog, ms.PROG = ms.PROG, full
            try:
                got = ms.death_state(m)
            finally:
                ms.PROG = prog
            if got != want or hdrs != got[8:10]:
                fatal("marble: death fixture replay differs, run marble_sim.py dfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    return out


# Phase 4b-0 tables the death code indexes (address, bytes): the fixture reads only parts
DEATH_TABLES = (
    (ms.SINE, 2 * 0x66),        # sine_pair 0..0x64, sine_lerp i + 1 <= 0x65 (d6 <= 0x1922)
    (ms.LERP, 2 * 0x35),        # lerp_1ee6e: i = (arg >> 10) - 0xc, i + 1 <= 0x34
    (ms.DOT_ANGLE, 2 * 39),     # dots_1c014: word 3 * ady + dx, ady <= 0xc, dx <= 2
    (ms.DOT_DY, 13),            # dots_1c014: ady <= 0xc
    (ms.ORBIT_STEPS, 16),       # sparkle star offsets
    (0x1d3f4, 16),              # dots_init_25b40 rest pose x, y
    (0x24a94, 6),               # timeout_1bbaa rng limit per race
    (0x24aae, 24),              # timeout_1bbaa zone list per race (0xff end, below)
    (ms.SCORES, 2 * 17),        # score_285b0 points: goal (race * 2 + 3), time bonus (0x10)
    (ms.SCORE_TEXTS, 4 * 17),   # score_285b0 score texts
)


def steelie_runs(mem, terrains, full):
    """Phase 4b-1: every steelie fixture segment and the steelie race sweep of every race
    (main tick with deaths, objects, sprites, draw list, steelies) on Mem class mem. Check
    values read the program ROM full (the MO model reads frame bytes the remake replaces by
    the sprite sets); every steelie frame drawn must be in its race set (0x21f36) or the
    death set (shatter). Returns crc32 of the work RAM per run."""
    if not os.path.exists(STEELIE_FIXTURE):
        fatal(f"marble: {STEELIE_FIXTURE} missing, run tools/make_fixture.py stwrite")

    def check(m, race):
        prog, ms.PROG = ms.PROG, full
        try:
            got = me.tick_state(m)
        finally:
            ms.PROG = prog
        for s in got[1:3]:
            hdr, shard = s[9], s[10]
            if hdr == mo.STEELIE_FRAME and hdr not in SPRITES[race].ids:
                fatal(f"marble: race {race} steelie frame not in its sprite set")
            if hdr not in (0, mo.STEELIE_FRAME) and hdr not in DEATH.ids or \
                    shard and shard not in DEATH.ids:
                fatal(f"marble: steelie frame {hdr:05x}/{shard:05x} not in the death set")
        return got

    out = []
    for seg in me.load_sfixture(STEELIE_FIXTURE):
        m = me.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            mo.boxes_and_sort(m)
            if check(m, seg["race"]) != want:
                fatal("marble: steelie fixture replay differs, run marble_enemy.py sfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    for n in range(md.LEVELS):
        m = me.setup(mem(terrains[n], bytes(0x2000)))
        me.sweep(m, n)
        check(m, n)
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def steelie_bytes():
    """Phase 4b-1 tables (whole): spawn table pointers and entries per race (0xff end), every
    node list of an entry up to its 0xff, 0xff end, the steelie and object pointer tables, the
    pair list, the push table 0x24ad6 (|v high word| < 8), the score tables of k = 3."""
    out = set(range(me.SPAWN_TABLE, me.SPAWN_TABLE + 4 * md.LEVELS))
    out |= set(range(me.OBJ_PTRS, me.OBJ_PTRS + 16)) | set(range(0x1effe, 0x1effe + 8))
    out |= set(range(me.PAIR_LIST, me.PAIR_LIST + 16)) | set(range(me.REPEL, me.REPEL + 8))
    out |= set(range(ms.SCORES + 6, ms.SCORES + 8)) | set(range(ms.SCORE_TEXTS + 12, ms.SCORE_TEXTS + 16))
    for race in range(md.LEVELS):
        e = ms.rom_l(me.SPAWN_TABLE + 4 * race)
        while ms.PROG[e] != 0xff:
            p = ms.rom_l(e + 2)
            while not (ms.PROG[p] == 0xff and ms.PROG[p + 1] == 0xff):
                p += 6
            out |= set(range(ms.rom_l(e + 2), p + 2)) | set(range(e, e + 8))
            e += 8
        out.add(e)
    return out


def slinky_runs(mem, terrains, full):
    """Phase 4b-2: every slinky fixture segment and the slinky race sweep of every race (main
    tick with deaths, objects, sprites, draw list, steelies, slinkies) on Mem class mem. Every
    slinky frame drawn must be in the death set. Returns crc32 of the work RAM per run."""
    if not os.path.exists(SLINKY_FIXTURE):
        fatal(f"marble: {SLINKY_FIXTURE} missing, run tools/make_fixture.py slwrite")

    def check(m, race):
        prog, ms.PROG = ms.PROG, full
        try:
            got = sl.tick_state(m)
        finally:
            ms.PROG = prog
        for s_ in got[3:7]:
            if s_[11] and s_[11] not in DEATH.ids:
                fatal(f"marble: slinky frame {s_[11]:05x} not in the death set")
        return got

    out = []
    for seg in sl.load_slfixture(SLINKY_FIXTURE):
        m = sl.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            mo.boxes_and_sort(m)
            if check(m, seg["race"]) != want:
                fatal("marble: slinky fixture replay differs, run marble_slinky.py slfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    for n in range(md.LEVELS):
        m = sl.setup(mem(terrains[n], bytes(0x2000)))
        sl.sweep(m, n)
        check(m, n)
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def acid_runs(mem, terrains, full):
    """Phase 4b-3: every acid fixture segment and the acid race sweep of every race (main tick
    with all modelled enemies) on Mem class mem. Every acid frame drawn must be in the death
    set. Returns crc32 of the work RAM per run."""
    if not os.path.exists(ACID_FIXTURE):
        fatal(f"marble: {ACID_FIXTURE} missing, run tools/make_fixture.py acwrite")

    def check(m):
        prog, ms.PROG = ms.PROG, full
        try:
            got = ac.tick_state(m)
        finally:
            ms.PROG = prog
        for a_ in got[1:8]:
            if a_[9] and a_[9] not in DEATH.ids:
                fatal(f"marble: acid frame {a_[9]:05x} not in the death set")
        return got

    out = []
    for seg in ac.load_acfixture(ACID_FIXTURE):
        m = me.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            mo.boxes_and_sort(m)
            if check(m) != want:
                fatal("marble: acid fixture replay differs, run marble_acid.py acfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    for n in range(md.LEVELS):
        m = me.setup(mem(terrains[n], bytes(0x2000)))
        ac.sweep(m, n)
        check(m)
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def bird_runs(mem, terrains, full):
    """Phase 4b-4a: every bird fixture segment and the bird race sweep of every race (main tick
    with all modelled enemies) on Mem class mem. Every bird frame drawn must be in the death
    set. Returns crc32 of the work RAM per run."""
    if not os.path.exists(BIRD_FIXTURE):
        fatal(f"marble: {BIRD_FIXTURE} missing, run tools/make_fixture.py bdwrite")

    def check(m):
        prog, ms.PROG = ms.PROG, full
        try:
            got = bd.tick_state(m)
        finally:
            ms.PROG = prog
        for b in got[1:11]:
            if b[10] and b[10] not in DEATH.ids:
                fatal(f"marble: bird frame {b[10]:05x} not in the death set")
        return got

    out = []
    for seg in bd.load_bdfixture(BIRD_FIXTURE):
        m = me.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            mo.boxes_and_sort(m)
            if check(m) != want:
                fatal("marble: bird fixture replay differs, run marble_bird.py bdfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    for n in range(md.LEVELS):
        m = me.setup(mem(terrains[n], bytes(0x2000)))
        bd.sweep(m, n)
        check(m)
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def tube_runs(mem, terrains, full):
    """Tubes: every tube fixture segment (main tick with all enemies, the RNG word an input); the
    check values read the program ROM full, as death_runs. Returns crc32 of the work RAM per
    segment."""
    if not os.path.exists(TUBE_FIXTURE):
        fatal(f"marble: {TUBE_FIXTURE} missing, run tools/make_fixture.py twrite")
    out = []
    for seg in ms.load_dfixture(TUBE_FIXTURE, True):
        m = me.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, rng, want in seg["ticks"]:
            m.ww(0x4003a6, rng)
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            prog, ms.PROG = ms.PROG, full
            try:
                got = ms.death_state(m)
            finally:
                ms.PROG = prog
            if got != want:
                fatal("marble: tube fixture replay differs, run marble_sim.py tfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def aerial_runs(mem, terrains, full):
    """Aerial objects: every aerial fixture segment (main tick with all enemies); the check
    values read the program ROM full, as death_runs. Returns crc32 of the work RAM per
    segment."""
    if not os.path.exists(AERIAL_FIXTURE):
        fatal(f"marble: {AERIAL_FIXTURE} missing, run tools/make_fixture.py awrite")
    out = []
    for seg in ms.load_dfixture(AERIAL_FIXTURE, False, "marble_aerials_segments"):
        m = me.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            prog, ms.PROG = ms.PROG, full
            try:
                got = ms.death_state(m)
            finally:
                ms.PROG = prog
            if got != want:
                fatal("marble: aerial fixture replay differs, run marble_sim.py afixture")
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def mini_runs(mem, terrains, full):
    """Phase 4b-4b: every mini fixture segment and the mini race sweep of every race (main tick
    with all modelled enemies) on Mem class mem. Every mini frame drawn must be in the death
    set. Returns crc32 of the work RAM per run."""
    if not os.path.exists(MINI_FIXTURE):
        fatal(f"marble: {MINI_FIXTURE} missing, run tools/make_fixture.py mnwrite")

    def check(m):
        prog, ms.PROG = ms.PROG, full
        try:
            got = mn.tick_state(m)
        finally:
            ms.PROG = prog
        for o in got[1:10]:
            if o[14] and o[14] not in DEATH.ids:
                fatal(f"marble: mini frame {o[14]:05x} not in the death set")
        return got

    out = []
    for seg in mn.load_mnfixture(MINI_FIXTURE):
        m = me.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            mo.boxes_and_sort(m)
            if check(m) != want:
                fatal("marble: mini fixture replay differs, run marble_mini.py mnfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    for n in range(md.LEVELS):
        m = me.setup(mem(terrains[n], bytes(0x2000)))
        mn.sweep(m, n)
        check(m)
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def piston_runs(mem, terrains, full):
    """Phase 4b-5: every piston fixture segment and the piston race sweep of every race (main
    tick with all modelled enemies) on Mem class mem. Every piston frame drawn must be in the
    death set. Returns crc32 of the work RAM per run."""
    if not os.path.exists(PISTON_FIXTURE):
        fatal(f"marble: {PISTON_FIXTURE} missing, run tools/make_fixture.py pswrite")

    def check(m):
        prog, ms.PROG = ms.PROG, full
        try:
            got = pp.fixture_state(pp.tick_state(m))
        finally:
            ms.PROG = prog
        for o in got[3:39]:
            if o[7] and o[7] not in DEATH.ids:
                fatal(f"marble: piston frame {o[7]:05x} not in the death set")
        return got

    out = []
    for seg in pp.load_psfixture(PISTON_FIXTURE):
        m = me.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            mo.boxes_and_sort(m)
            if check(m) != want:
                fatal("marble: piston fixture replay differs, run marble_piston.py psfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    for n in range(md.LEVELS):
        m = me.setup(mem(terrains[n], bytes(0x2000)))
        pp.sweep(m, n)
        check(m)
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def piston_bytes():
    """Phase 4b-5 tables (whole): first and reload timers, the 4 variant pointers of both
    kinds and their 12 word tables, cells, tops, the animation list (0xffffffff end)."""
    out = set(range(pp.TIMER0, pp.TIMER0 + 2 * pp.FIXED)) | set(range(pp.TIMER, pp.TIMER + 2 * pp.FIXED))
    for base in (pp.VARIANT0, pp.VARIANT):
        out |= set(range(base, base + 16))
        for v in range(4):
            a = ms.rom_l(base + 4 * v)
            out |= set(range(a, a + 2 * (len(pp.PISTONS) - pp.FIXED)))
    out |= set(range(pp.CELL_X, pp.CELL_X + 36)) | set(range(pp.CELL_Y, pp.CELL_Y + 36))
    out |= set(range(pp.TOP, pp.TOP + 72))
    for a in mo.PISTON_LISTS:
        out |= set(range(a, a + 4 * len(mo.anim_list(a)) + 4))
    return out


def mini_bytes():
    """Phase 4b-4b tables (whole): the mini pointer table, the step tables 0x244b6/0x244d6,
    the cell bitmap 0x24496, the animation lists (0xffffffff end)."""
    out = set(range(0x1f096, 0x1f096 + 36)) | set(range(mn.GRID, mn.GRID + 32))
    out |= set(range(mn.DX, mn.DX + 32)) | set(range(mn.DY, mn.DY + 32))
    for a in mo.MINI_LISTS:
        out |= set(range(a, a + 4 * len(mo.anim_list(a)) + 4))
    return out


def bird_bytes():
    """Phase 4b-4a tables (whole): the bird pointer table, the spawn cells, the animation
    lists (0xffffffff end)."""
    out = set(range(0x1f0ba, 0x1f0ba + 40)) | set(range(bd.POINTS, bd.POINTS + 10))
    for a in mo.BIRD_LISTS:
        out |= set(range(a, a + 4 * len(mo.anim_list(a)) + 4))
    # silly race tubes: 0x1cd00 shape 5 (pipe exits 0x23/0x24), pointer and plane entries
    shape = ms.rom_l(ms.SHAPES + 4 * 5)
    out |= set(range(ms.SHAPES + 4 * 5, ms.SHAPES + 4 * 6)) | set(range(shape, shape + 3 * 16))
    return out


def acid_bytes():
    """Phase 4b-3 tables (whole): spawn table pointers and entries per race (0xff end), the
    random path lists, every path up to its 0xff end, the acid pointer table, the animation
    lists (0xffffffff end)."""
    out = set(range(ac.SPAWN_TABLE, ac.SPAWN_TABLE + 4 * md.LEVELS))
    out |= set(range(0x1f07a, 0x1f07a + 28))
    out |= set(range(ac.SINGLE[0], ac.GROUP[1] + 24))
    for a in mo.ACID_LISTS:
        out |= set(range(a, a + 4 * len(mo.anim_list(a)) + 4))
    paths = [ms.rom_l(t) for t in range(ac.SINGLE[0], ac.GROUP[1] + 24, 4)]
    for race in range(md.LEVELS):
        e = ms.rom_l(ac.SPAWN_TABLE + 4 * race)
        while ms.PROG[e] != 0xff:
            out |= set(range(e, e + 8))
            if ms.rom_l(e + 2): paths.append(ms.rom_l(e + 2))
            e += 8
        out.add(e)
    for p in paths:
        q = p
        while ms.PROG[q] != 0xff:
            q += 4
        out |= set(range(p, q + 1))
    return out


def slinky_bytes():
    """Phase 4b-2 tables (whole): spawn table pointers and entries per race (0xff end), every
    entry's waypoint list up to its 0xff end, the slinky pointer table, the animation lists
    (0xffffffff end), the lunge height table."""
    out = set(range(sl.SPAWN_TABLE, sl.SPAWN_TABLE + 4 * md.LEVELS))
    out |= set(range(0x1f006, 0x1f006 + 16)) | set(range(sl.REAR, sl.REAR + 0x20))
    for a in mo.SLINKY_LISTS:
        out |= set(range(a, a + 4 * len(mo.anim_list(a)) + 4))
    for race in range(md.LEVELS):
        e = ms.rom_l(sl.SPAWN_TABLE + 4 * race)
        while ms.PROG[e] != 0xff:
            p = ms.rom_l(e + 2)
            while ms.PROG[p] != 0xff and ms.PROG[p + 1] != 0xff:
                p += 4
            out |= set(range(ms.rom_l(e + 2), p + 2)) | set(range(e, e + 8))
            e += 8
        out.add(e)
    return out


def death_bytes():
    """Phase 4b-0 tables (whole): the player animation lists (0xffffffff end included), the
    reached shatter overlay entries (the remake reads the frame headers from them), the tables
    of DEATH_TABLES and the time out zone lists."""
    out = set()
    for a in mo.DEATH_LISTS:
        out |= set(range(a, a + 4 * len(mo.anim_list(a)) + 4))
    out |= set(range(ms.ANIM_SHARDS, ms.ANIM_SHARDS + 4 * DEATH.shards))
    for a, length in DEATH_TABLES:
        out |= set(range(a, a + length))
    for race in range(md.LEVELS):
        z = ms.rom_l(0x24aae + 4 * race)
        end = z
        while ms.PROG[end] != 0xff:
            end += 1
        out |= set(range(z, end + 1))
    return out


def sprite_bytes():
    """Phase 4a tables (whole) as a byte address set: frame header box bytes +4..+7 of every
    frame of every mode 0/2 animation list (0x1b12a boxes of the draw list entries, tags
    drawn or not), the empty animation (0x20c14, its header read at
    0xffffffff + 4 = ROM 0x3-0x6), the tag 3 palette command bytes 0x1df18."""
    out = set(range(3, 7)) | set(range(ms.ANIM_NONE, ms.ANIM_NONE + 4))
    out |= set(range(mo.TAG3_PAL, mo.TAG3_PAL + mo.TAG3_FRAMES))
    for race in range(md.LEVELS):
        for e, h in mo.box_headers(race):
            out |= set(range(e, e + 4))
            if h != 0xffffffff: out |= set(range(h + 4, h + 8))
        for h in SPRITES[race].hdrs:
            out |= set(range(h + 4, h + 8))
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
    """Program ROM ranges the sim reads. Returns (blocks, phase 2 blocks, reference runs,
    phase 3 blocks, phase 4a blocks): phase 2 = physics reads plus the Phase 2 tables, phase 3
    = phase 2 plus everything the object and occlusion runs read plus the Phase 3 tables, 4a =
    phase 3 plus the sprite runs and the Phase 4a tables, all = 4a plus the death runs and the
    Phase 4b-0 tables, steelies = all plus the steelie runs and the Phase 4b-1 tables,
    slinkies = steelies plus the slinky runs and the Phase 4b-2 tables, acid = slinkies plus
    the acid runs and the Phase 4b-3 tables, birds = acid plus the bird and tube runs and the
    Phase 4b-4a tables, minis = birds plus the mini runs and the Phase 4b-4b tables, all =
    minis plus the piston and aerial object runs and the Phase 4b-5 tables."""
    covered = table_bytes([t.desc for t in terrains])
    phase3 = phase3_bytes()      # before ms.PROG is replaced
    sprite_tab = sprite_bytes()
    death_tab = death_bytes()
    steelie_tab = steelie_bytes()
    slinky_tab = slinky_bytes()
    acid_tab = acid_bytes()
    bird_tab = bird_bytes()
    mini_tab = mini_bytes()
    piston_tab = piston_bytes()
    ms.EDGES = lambda race, ult, c, d: edge_lookup(EDGE_TABLES, race, ult, c, d)
    plain = ms.PROG
    ms.PROG = RecProg(plain)
    try:
        RecProg.reads.clear(); RecMem.slap.clear()
        ref2 = physics_runs(RecMem, terrains)
        want = set(RecProg.reads)
        ref3 = physics_runs(RecMem, terrains, True) + object_runs(RecMem, terrains)
        want3 = set(RecProg.reads)
        ref4 = sprite_runs(RecMem, terrains)
        want4 = set(RecProg.reads)
        ref5 = death_runs(RecMem, terrains, plain)
        want5 = set(RecProg.reads)
        ref6 = steelie_runs(RecMem, terrains, plain)
        want6 = set(RecProg.reads)
        ref7 = slinky_runs(RecMem, terrains, plain)
        want7 = set(RecProg.reads)
        ref8 = acid_runs(RecMem, terrains, plain)
        want8 = set(RecProg.reads)
        ref9 = bird_runs(RecMem, terrains, plain) + tube_runs(RecMem, terrains, plain)
        want9 = set(RecProg.reads)
        ref10 = mini_runs(RecMem, terrains, plain)
        want10 = set(RecProg.reads)
        ref11 = piston_runs(RecMem, terrains, plain) + aerial_runs(RecMem, terrains, plain)
        want11 = set(RecProg.reads)
    finally:
        ms.PROG = plain
        ms.EDGES = None
    outside = sorted(want - covered)
    if outside:
        fatal("marble: physics reads ROM outside the known tables: "
              + ", ".join(f"{a:06x}" for a in outside[:16]))
    p2 = [(a, bytes(plain[a:e])) for a, e in merge(want | covered)]
    blocks3 = [(a, bytes(plain[a:e])) for a, e in merge(want3 | covered | phase3)]
    blocks4 = [(a, bytes(plain[a:e])) for a, e in merge(want4 | covered | phase3 | sprite_tab)]
    blocks4b = [(a, bytes(plain[a:e]))
                for a, e in merge(want5 | covered | phase3 | sprite_tab | death_tab)]
    blocks4b1 = [(a, bytes(plain[a:e])) for a, e in
                 merge(want6 | covered | phase3 | sprite_tab | death_tab | steelie_tab)]
    blocks4b2 = [(a, bytes(plain[a:e])) for a, e in
                 merge(want7 | covered | phase3 | sprite_tab | death_tab | steelie_tab | slinky_tab)]
    blocks4b3 = [(a, bytes(plain[a:e])) for a, e in
                 merge(want8 | covered | phase3 | sprite_tab | death_tab | steelie_tab
                       | slinky_tab | acid_tab)]
    blocks4b4 = [(a, bytes(plain[a:e])) for a, e in
                 merge(want9 | covered | phase3 | sprite_tab | death_tab | steelie_tab
                       | slinky_tab | acid_tab | bird_tab)]
    blocks4b5 = [(a, bytes(plain[a:e])) for a, e in
                 merge(want10 | covered | phase3 | sprite_tab | death_tab | steelie_tab
                       | slinky_tab | acid_tab | bird_tab | mini_tab)]
    allb = [(a, bytes(plain[a:e])) for a, e in
            merge(want11 | covered | phase3 | sprite_tab | death_tab | steelie_tab | slinky_tab
                  | acid_tab | bird_tab | mini_tab | piston_tab)]
    dyn_only = want3 - covered - phase3
    if dyn_only:
        print(f"phase 3 runs read {len(dyn_only)} B outside the tables: "
              + ", ".join(f"{a:05x}+{e - a}" for a, e in merge(dyn_only, 0)))
    sprite_only = want4 - want3 - covered - phase3 - sprite_tab
    if sprite_only:
        print(f"phase 4a runs read {len(sprite_only)} B outside the tables: "
              + ", ".join(f"{a:05x}+{e - a}" for a, e in merge(sprite_only, 0)))
    death_only = want5 - want4 - covered - phase3 - sprite_tab - death_tab
    if death_only:
        print(f"phase 4b-0 runs read {len(death_only)} B outside the tables: "
              + ", ".join(f"{a:05x}+{e - a}" for a, e in merge(death_only, 0)))
    steelie_only = want6 - want5 - covered - phase3 - sprite_tab - death_tab - steelie_tab
    if steelie_only:
        print(f"phase 4b-1 runs read {len(steelie_only)} B outside the tables: "
              + ", ".join(f"{a:05x}+{e - a}" for a, e in merge(steelie_only, 0)))
    slinky_only = want7 - want6 - covered - phase3 - sprite_tab - death_tab - steelie_tab \
        - slinky_tab
    if slinky_only:
        print(f"phase 4b-2 runs read {len(slinky_only)} B outside the tables: "
              + ", ".join(f"{a:05x}+{e - a}" for a, e in merge(slinky_only, 0)))
    acid_only = want8 - want7 - covered - phase3 - sprite_tab - death_tab - steelie_tab \
        - slinky_tab - acid_tab
    if acid_only:
        print(f"phase 4b-3 runs read {len(acid_only)} B outside the tables: "
              + ", ".join(f"{a:05x}+{e - a}" for a, e in merge(acid_only, 0)))
    bird_only = want9 - want8 - covered - phase3 - sprite_tab - death_tab - steelie_tab \
        - slinky_tab - acid_tab - bird_tab
    if bird_only:
        print(f"phase 4b-4a runs read {len(bird_only)} B outside the tables: "
              + ", ".join(f"{a:05x}+{e - a}" for a, e in merge(bird_only, 0)))
    mini_only = want10 - want9 - covered - phase3 - sprite_tab - death_tab - steelie_tab \
        - slinky_tab - acid_tab - bird_tab - mini_tab
    if mini_only:
        print(f"phase 4b-4b runs read {len(mini_only)} B outside the tables: "
              + ", ".join(f"{a:05x}+{e - a}" for a, e in merge(mini_only, 0)))
    piston_only = want11 - want10 - covered - phase3 - sprite_tab - death_tab - steelie_tab \
        - slinky_tab - acid_tab - bird_tab - mini_tab - piston_tab
    if piston_only:
        print(f"phase 4b-5 runs read {len(piston_only)} B outside the tables: "
              + ", ".join(f"{a:05x}+{e - a}" for a, e in merge(piston_only, 0)))
    return (allb, p2, ref2 + ref3 + ref4 + ref5 + ref6 + ref7 + ref8 + ref9 + ref10 + ref11,
            blocks3, blocks4, blocks4b, blocks4b1, blocks4b2, blocks4b3, blocks4b4, blocks4b5)


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
    pals = {tuple(md.palette(rom, n, True)[0x100 + cs * 16:0x110 + cs * 16]) for n in range(md.LEVELS)}
    if len(pals) != 1:
        fatal("marble: motion object palette differs between races")
    return tiles, [rgb888_to_rgb565_le(*c) for c in pals.pop()]


def c_bytes(b, indent="  "):
    return ",\n".join(indent + ",".join(f"0x{v:02x}" for v in b[k:k + 24])
                      for k in range(0, len(b), 24))


def c_words(w, indent="  "):
    return ",\n".join(indent + ",".join(f"0x{v:04x}" for v in w[k:k + 16])
                      for k in range(0, len(w), 16))


def emit_physics(f, rom, size3, size4, size4b, size4b1, size4b2, size4b3, size4b4, size4b5,
                 size4b6):
    """Phase 2 data into marble_rom.h. Returns {item: flash bytes}; the prog blocks added by
    Phase 3 go to size3, by Phase 4a to size4, by Phase 4b-0 to size4b, by 4b-1 to size4b1,
    by 4b-2 to size4b2, by 4b-3 to size4b3, by 4b-4a to size4b4, by 4b-4b to size4b5, by 4b-5
    to size4b6."""
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
    print("// record codes 0x0001-0x07ff read records[code + quadrant]. packed unpacks to the",
          file=f)
    print("// base height words per diagonal (maxd, little endian), then the index (pairs * 54).",
          file=f)
    print("struct MarbleTerrainInfo {\n  FlashAsset<unsigned char, COMPRESSED> packed; "
          "uint16_t pairs;\n"
          "  const uint16_t *codes; uint16_t ncodes;\n  const uint8_t *records;\n"
          "  uint16_t maxd;\n  uint16_t nrecords; uint32_t desc;\n"
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
    k = {}   # read back what is emitted: every list at its offset, ending in the pool
    for race, cols in EDGE_TABLES:
        row = offs[race][k.setdefault(race, 0)]
        k[race] += 1
        for lst, o in zip(cols, row):
            if bytes(pool[o:o + len(lst)]) != bytes(lst):
                fatal(f"marble: race {race} emitted edge list differs at pool offset {o}")
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
    unpacked = 0
    for n, (index, codes, records, _, bases) in enumerate(races):
        if len(bases) != terrains[n].maxd or len(index) != terrains[n].maxd // 2 * PAIR_BYTES:
            fatal(f"marble: race {n} bases/index do not match its {terrains[n].maxd} diagonals")
        raw = struct.pack(f"<{len(bases)}H", *bases) + index
        unpacked = max(unpacked, len(raw))
        emit_compressed(f, f"marble_terrain_{n}", "unsigned char", "", len(raw), list(raw))
        size["terrain index + bases"] = (size.get("terrain index + bases", 0)
                                         + len(lzma_compress(raw)))
        key = tuple(codes)
        if key not in code_names:
            code_names[key] = f"marble_terrain_codes_{n}"
            print(f"static const uint16_t {code_names[key]}[] = {{\n{c_words(codes)}\n}};", file=f)
            size["terrain codes"] = size.get("terrain codes", 0) + 2 * len(codes)
        print(f"static const uint8_t marble_terrain_records_{n}[] = {{\n{c_bytes(records)}\n}};",
              file=f)
        size["terrain records"] = size.get("terrain records", 0) + len(records)
    print("// bytes of the largest unpacked race terrain (Arena buffer)", file=f)
    print(f"static const uint16_t MARBLE_TERRAIN_MAX = {unpacked};", file=f)
    print("static const MarbleTerrainInfo marble_terrain[6] = {", file=f)
    for n, (index, codes, records, _, bases) in enumerate(races):
        t = terrains[n]
        print(f"  {{ marble_terrain_{n}_blob, {len(index) // PAIR_BYTES}, "
              f"{code_names[tuple(codes)]}, {len(codes)}, marble_terrain_records_{n}, "
              f"{t.maxd}, {len(records)}, 0x{t.desc:05x}, "
              f"marble_edge_off_{n}, {len(offs[n])} }},", file=f)
    print("};", file=f)
    size["terrain info"] = 6 * 32  # 32 bit pointers, padded

    BlockMem.records = {n: (r[3], r[2]) for n, r in enumerate(races)}
    blocks, blocks2, ref, blocks3, blocks4, blocks4b, blocks4b1, blocks4b2, blocks4b3, \
        blocks4b4, blocks4b5 = prog_blocks(terrains)
    for n in range(md.LEVELS):
        recs, data = BlockMem.records[n]
        if any(not recs <= a < recs + len(data) for a in RecMem.slap.get(n, ())):
            fatal(f"marble: race {n} reads slapstic data outside its terrain records")
    plain = ms.PROG
    ms.PROG = BlockProg(blocks)
    ms.EDGES = lambda race, ult, c, d: edge_lookup(EDGE_TABLES, race, ult, c, d)
    try:
        got = (physics_runs(BlockMem, terrains) + physics_runs(BlockMem, terrains, True)
               + object_runs(BlockMem, terrains) + sprite_runs(BlockMem, terrains)
               + death_runs(BlockMem, terrains, plain) + steelie_runs(BlockMem, terrains, plain)
               + slinky_runs(BlockMem, terrains, plain) + acid_runs(BlockMem, terrains, plain)
               + bird_runs(BlockMem, terrains, plain) + tube_runs(BlockMem, terrains, plain)
               + mini_runs(BlockMem, terrains, plain) + piston_runs(BlockMem, terrains, plain)
               + aerial_runs(BlockMem, terrains, plain))
    finally:
        ms.PROG = plain
        ms.EDGES = None
    if got != ref:
        fatal("marble: simulation on the emitted blocks differs from the full ROM")
    print("// Program ROM bytes the physics, level objects and occlusion read (68010 address,",
          file=f)
    print("// sorted). Records and terrain come from marble_terrain; any other ROM address is a",
          file=f)
    print("// porting error. Block k is at off in marble_prog (all blocks joined).", file=f)
    print("struct MarbleProgBlock { uint32_t addr; uint16_t len; uint16_t off; };", file=f)
    joined = b"".join(data for _, data in blocks)
    if len(joined) > 0xffff:
        fatal("marble: prog blocks too long for 16 bit offsets")
    emit_compressed(f, "marble_prog", "unsigned char", "", len(joined), list(joined))
    print(f"static const MarbleProgBlock marble_prog_blocks[{len(blocks)}] = {{", file=f)
    off = 0
    for a, data in blocks:
        print(f"  {{ 0x{a:05x}, {len(data)}, {off} }},", file=f)
        off += len(data)
    print(f"}};\nstatic const uint8_t MARBLE_PROG_BLOCKS = {len(blocks)};", file=f)
    flash = lambda bl: len(lzma_compress(b"".join(d for _, d in bl))) + 8 * len(bl)
    size["prog blocks"] = flash(blocks2)
    size3["prog blocks"] = flash(blocks3) - flash(blocks2)
    size4["prog blocks"] = flash(blocks4) - flash(blocks3)
    size4b["prog blocks"] = flash(blocks4b) - flash(blocks4)
    size4b1["prog blocks"] = flash(blocks4b1) - flash(blocks4b)
    size4b2["prog blocks"] = flash(blocks4b2) - flash(blocks4b1)
    size4b3["prog blocks"] = flash(blocks4b3) - flash(blocks4b2)
    size4b4["prog blocks"] = flash(blocks4b4) - flash(blocks4b3)
    size4b5["prog blocks"] = flash(blocks4b5) - flash(blocks4b4)
    size4b6["prog blocks"] = flash(blocks) - flash(blocks4b5)

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


SHADE_COLOURS = (0x343, 0x3f2, 0x3f3)   # palette entries the wave strip pens 2/3 select
HDR_BASE = 0x20000                      # frame header address table: (address - base) >> 1
LIFT_PAL_FIRST = 0x161                  # tag 3 palette variants: set 6 pens 1-4


def sprite_set(rom, sp):
    """Tile pixels (4 bpp, 32 B per tile: left pixel in the high nibble) of a race's sprite
    set, in sorted code order. A frame column of tiles c .. c + h - 1 reads consecutive
    tiles, so identical tiles are not merged (LZMA folds them at no index cost)."""
    out = bytearray()
    for code in sp.tile_codes:
        tile, cs = md.mo_lookup(rom, code)
        px = md.mo_tile(rom, tile)
        out += bytes(px[k] << 4 | px[k + 1] for k in range(0, 64, 2))
    return bytes(out)


def sprite_check_codes(rom, sp, sets=None):
    """A column code carries its palette set in bits 11-13 (ORed by 0x26f3e): the tile and the
    set the lookup returns must equal the plain tile code (checked for the sets in use)."""
    for code in sp.tile_codes:
        for st in sets or SETS_USED:
            t, cs = md.mo_lookup(rom, code | st << 11)
            if t != md.mo_lookup(rom, code)[0] or cs != st:
                fatal(f"marble: MO code {code:03x} with set {st} looks up tile {t:03x}, set {cs}")


SETS_USED = mo.SETS


def lift_palette(rom):
    """The four tag 3 palette variants (0x20ae4 commands) as RGB565 for set 6 pens 1-4."""
    out = []
    for c in range(4):
        d = ms.rom_l(mo.PAL_CMDS + 4 * c)
        n = ms.PROG[d]
        dest = [(ms.rom_l(mo.PAL_DESTS + ms.PROG[d + 1] + 4 * k) - 0xb00000) // 2
                for k in range(n)]
        if dest != [LIFT_PAL_FIRST + k for k in range(4)]:
            fatal(f"marble: lift palette command {c} writes {dest}")
        out.append([rgb888_to_rgb565_le(*md.irgb_hw(ms.rom_w(d + 2 + 2 * k))) for k in range(4)])
    return out


def frame_table(f, name, sp, base):
    """Frame table of sprite set sp (addr, frames, cols arrays with suffix name), column tiles
    + base (sprite tile buffer index). Returns the column pool."""
    pool, first, seen = [], [], {}
    for fid in range(len(sp.cols)):
        cols = tuple((x, y, hh, tile + base) for x, y, hh, tile in sp.objs_cols(fid))
        for x, y, hh, tile in cols:
            if not (-128 <= x < 128 and -128 <= y < 128):
                fatal(f"marble: sprite column offset {x},{y} does not fit int8")
        if cols not in seen:
            seen[cols] = len(pool); pool += cols
        first.append(seen[cols])
    for h in sp.hdrs:
        if h & 1 or not HDR_BASE <= h < HDR_BASE + 0x20000:
            fatal(f"marble: frame header {h:05x} odd or outside 0x20000-0x3ffff")
    addr = [(h - HDR_BASE) >> 1 for h in sp.hdrs]
    print(f"static const uint16_t marble_sprite_addr_{name}[] = {{\n{c_words(addr)}\n}};", file=f)
    print(f"static const MarbleSpriteFrame marble_sprite_frames_{name}[] = {{", file=f)
    for fid, c in enumerate(sp.cols):
        print(f"  {{ {len(c)}, {first[fid]} }},", file=f)
    print("};", file=f)
    print(f"static const MarbleSpriteCol marble_sprite_cols_{name}[] = {{", file=f)
    for x, y, hh, tile in pool:
        print(f"  {{ {x}, {y}, {hh}, {tile} }},", file=f)
    print("};", file=f)
    return pool


def sets_flash(rom, sprites):
    """Flash bytes of the distinct sprite sets of sprites (tiles LZMA + 16, frames, deduplicated
    columns, header ids), as emit_sprites counts them."""
    total, keys = 0, set()
    for sp in sprites:
        tiles = sprite_set(rom, sp)
        key = (tuple(sp.hdrs), tuple(tuple(c) for c in sp.cols), tuple(sp.tile_codes), tiles)
        if key in keys:
            continue
        keys.add(key)
        pool = {tuple(sp.objs_cols(fid)) for fid in range(len(sp.cols))}
        total += len(lzma_compress(tiles)) + 16 + 4 * len(sp.cols) + 2 * len(sp.hdrs)
        total += 6 * sum(len(c) for c in pool)
    return total


def emit_sprites(f, rom, size4, size4b1, size4b2):
    """Phase 4a: level object sprites per race (MARBLE.md "Level object sprites"): tile set,
    frame table, palettes. Races with the same frames and tiles share one set."""
    sets, race_set, keys = [], [], {}
    for n, sp in enumerate(SPRITES):
        sprite_check_codes(rom, sp)
        tiles = sprite_set(rom, sp)
        key = (tuple(sp.hdrs), tuple(tuple(c) for c in sp.cols), tuple(sp.tile_codes), tiles)
        if key not in keys:
            keys[key] = len(sets)
            sets.append((n, sp, tiles))
        race_set.append(keys[key])
    print("\n// ---- Phase 4a: level object sprites (MARBLE.md \"Level object sprites\")", file=f)
    for line in (
            "// Sprite set per race (races with the same frames and tiles share one): the 4 bpp",
            "// tiles of every MO code the race's slots draw (sorted code order, 32 B per tile, left",
            "// pixel in the high nibble, pens 0-7, LZMA like the playfield tiles). Frame ids: k <",
            "// nhdr is the frame whose MO header is at 0x20000 + addr[k] * 2 (68010 address,",
            "// ascending); nhdr + i (i < 23) is wave strip entry i (priority shade MOs, 0x27fa2;",
            "// ncols 0: none). A column shows tiles tile .. tile + (h & 0x7f) - 1 top down, hflip",
            "// h bit 7, top left at (slot x + x, slot y + y) in race pixels (y = -(MO y offset +",
            "// 8 h), MO y offset above sy + 16). Slot origin: x = sx (tag 5: sx - 1), y = camera -",
            "// descriptor +0x10 + 240 - sy. `tile` indexes the sprite tile buffer: the death set",
            "// first (MARBLE_DEATH_TILES), the race set's tiles after it; ntiles = buffer tiles the",
            "// set reaches."):
        print(line, file=f)
    print("struct MarbleSpriteCol { int8_t x, y; uint8_t h; uint16_t tile; };", file=f)
    print("struct MarbleSpriteFrame { uint8_t ncols; uint16_t first; };", file=f)
    print("struct MarbleSpriteSet {\n  const MarbleSpriteFrame *frames; const MarbleSpriteCol *cols;\n"
          "  const uint16_t *addr; uint8_t nhdr, nframes; uint16_t ntiles;\n};", file=f)
    sizes = {"tiles": 0, "frames": 0, "columns": 0, "header ids": 0}
    ndeath = len(DEATH.tile_codes)
    for k, (n, sp, tiles) in enumerate(sets):
        pool = frame_table(f, k, sp, ndeath)
        emit_compressed(f, f"marble_spr{k}_tiles", "unsigned char", "[32]", len(sp.tile_codes),
                        list(tiles))
        packed = len(lzma_compress(tiles))
        names = "/".join(md.NAMES[r] for r in range(md.LEVELS) if race_set[r] == k)
        print(f"sprite set {k} ({names}): {len(sp.hdrs)} frame headers + "
              f"{len(sp.cols) - len(sp.hdrs)} strip entries, {len(sp.tile_codes)} tiles, "
              f"{len(tiles)} B raw (Arena), {packed} B LZMA, {len(pool)} columns "
              f"(of {sum(len(c) for c in sp.cols)})")
        sizes["tiles"] += packed + 16
        sizes["frames"] += 4 * len(sp.cols)
        sizes["columns"] += 6 * len(pool)
        sizes["header ids"] += 2 * len(sp.hdrs)
    print("static const MarbleSpriteSet marble_sprites[%d] = {" % len(sets), file=f)
    for k, (n, sp, tiles) in enumerate(sets):
        print(f"  {{ marble_sprite_frames_{k}, marble_sprite_cols_{k}, marble_sprite_addr_{k}, "
              f"{len(sp.hdrs)}, {len(sp.cols)}, {ndeath + len(sp.tile_codes)} }},", file=f)
    print("};", file=f)
    print("static const FlashAsset<unsigned char[32], COMPRESSED> marble_sprite_tiles[%d] = { "
          % len(sets) + ", ".join(f"marble_spr{k}_tiles_blob" for k in range(len(sets))) + " };",
          file=f)
    print("// Sprite set of each race; tiles of the sprite tile buffer (Arena): death set + largest",
          file=f)
    print("// race set.", file=f)
    print("static const uint8_t marble_sprite_race[6] = { "
          + ", ".join(str(k) for k in race_set) + " };", file=f)
    print("static const uint16_t MARBLE_SPRITE_MAX_TILES = "
          f"{ndeath + max(len(s[1].tile_codes) for s in sets)};", file=f)
    print("// Palette per race, set 3, 5, 6, 7 (MO palette RAM 0x100 + set * 16), pens 0-7 as", file=f)
    print("// RGB565 byte swapped; the wave strip shade colours (palette entries 0x343, 0x3f2,", file=f)
    print("// 0x3f3 = pen 3 over playfield pixel 4, pen 2 and 3 over pixel 15; every other 0x3xx", file=f)
    print("// entry is black); the lift palette variants 0-3 (set 6 pens 1-4).", file=f)
    pals, shade = [], []
    for n in range(md.LEVELS):
        rgb = md.palette(rom, n, True)
        pals.append([[rgb888_to_rgb565_le(*rgb[0x100 + st * 16 + pen]) for pen in range(8)]
                     for st in mo.SETS])
        for e in range(0x300, 0x400):
            if e not in SHADE_COLOURS and rgb[e] != (0, 0, 0):
                fatal(f"marble: race {n} shade palette entry {e:03x} not black")
        shade.append([rgb888_to_rgb565_le(*rgb[e]) for e in SHADE_COLOURS])
    print("static const uint16_t marble_sprite_pal[6][4][8] = {", file=f)
    for pr in pals:
        print("  {" + ", ".join("{" + ",".join(f"0x{v:04x}" for v in row) + "}" for row in pr)
              + "},", file=f)
    print("};", file=f)
    print("static const uint16_t marble_sprite_shade[6][3] = {", file=f)
    for sh in shade:
        print("  {" + ",".join(f"0x{v:04x}" for v in sh) + "},", file=f)
    print("};", file=f)
    lift = lift_palette(rom)
    print("static const uint16_t marble_lift_pal[4][4] = {", file=f)
    for row in lift:
        print("  {" + ",".join(f"0x{v:04x}" for v in row) + "},", file=f)
    print("};", file=f)
    sizes["palettes"] = 2 * (6 * 32 + 6 * 3 + 16)
    sizes["set table"] = 16 * len(sets) + 6
    size4.update(sizes)
    plain = [mo.Sprites(n, steelies=False) for n in range(md.LEVELS)]
    size4b1["sprite sets"] = sets_flash(rom, SPRITES) - sets_flash(rom, plain)
    print("sprite frame ids per race: " + ", ".join(
        f"{md.NAMES[r]} {len(SPRITES[r].cols)}" for r in range(md.LEVELS)))


DEATH_SETS = (0, 2)   # MO palette sets of the death frames: player 1 (+0x19 = 0), overlay 0x1000
STEELIE_SET = 5       # a shattering steelie draws the shatter frames in set 5 (race palette)
SLINKY_SET = 4        # slinky frames (death set), palette equal in the slinky races 1, 2, 5
SLINKY_RACES = (1, 2, 5)
ACID_SET = 6          # acid frames (death set), palette equal in the acid races 2, 5
ACID_RACES = (2, 5)
BIRD_SET = 7          # silly race birds (death set), palette set 7 of the silly race
BIRD_RACES = (4,)
MINI_SETS = (5, 6, 4)  # silly race minis of type 7, 8, 9 (death set rows 5, 6, 7)
PISTON_SET = 4        # aerial race pistons (death set row 8), palette set 4 of the aerial race
PISTON_RACES = (3,)


def race_pal(rom, st, races, what):
    """Palette set st, pens 0-7 RGB565, equal in races (else fatal)."""
    pal = {tuple(rgb888_to_rgb565_le(*md.palette(rom, n, True)[0x100 + st * 16 + pen])
                 for pen in range(8)) for n in races}
    if len(pal) != 1:
        fatal(f"marble: {what} palette differs between the {what} races")
    return pal.pop()


def hud_data(rom):
    """Phase 3b (MARBLE.md "HUD"): glyph subset, code map, palettes as RGB565."""
    glyphs = hud.font()
    codes = [c for c in hud.glyph_codes(rom) if any(any(line) for line in glyphs[c])]
    rows = [hud.glyph_rows(glyphs[c]) for c in codes]
    gmap = [hud.NO_GLYPH] * hud.CODES
    for k, c in enumerate(codes):
        gmap[c] = k
    rgb = lambda w: rgb888_to_rgb565_le(*md.irgb_hw(w))
    pal = [rgb(md.u16(rom, md.PAL_ALPHA + 2 * i)) for i in range(32)]
    pulse = [rgb(md.u16(rom, hud.PULSE + 2 * i)) for i in range(16)]
    return codes, rows, gmap, pal, pulse, rgb


def emit_hud(f, rom, size3b):
    codes, rows, gmap, pal, pulse, rgb = hud_data(rom)
    print("\n// ---- Phase 3b: HUD (MARBLE.md \"HUD\", tools/marble_hud.py)", file=f)
    for line in (
            "// Alpha font subset (136032.104.f5): 8 rows per glyph, 2 bpp, left pixel in bits",
            "// 15-14, pens 0-3 (0 transparent unless opaque). marble_hud_glyph maps an alpha code",
            "// (0-0x7f small font, 0x100-0x17f big font) to its glyph, MARBLE_HUD_NO_GLYPH: blank.",
            "// Palette: colour * 4 + pen, ROM 0x1FBD0; pulse table 0x20b34 (score fade, timer",
            "// flash); bonus colours of 0x118d2."):
        print(line, file=f)
    print(f"static const uint8_t MARBLE_HUD_NO_GLYPH = 0x{hud.NO_GLYPH:02x};", file=f)
    print(f"static const uint16_t marble_hud_glyphs[{len(rows)}][8] = {{", file=f)
    for c, g in zip(codes, rows):
        print("  { " + ", ".join(f"0x{v:04x}" for v in g) + f" }},  // 0x{c:03x}", file=f)
    print("};", file=f)
    print(f"static const uint8_t marble_hud_glyph[0x{hud.CODES:x}] = {{", file=f)
    for k in range(0, hud.CODES, 16):
        print("  " + ", ".join(f"0x{v:02x}" for v in gmap[k:k + 16]) + ",", file=f)
    print("};", file=f)
    print("static const uint16_t marble_hud_pal[32] = { "
          + ", ".join(f"0x{v:04x}" for v in pal) + " };", file=f)
    print("static const uint16_t marble_hud_pulse[16] = { "
          + ", ".join(f"0x{v:04x}" for v in pulse) + " };", file=f)
    print(f"static const uint16_t MARBLE_HUD_BONUS_GREY = 0x{rgb(0xafff):04x};  // 0x118d2", file=f)
    for name, addr in hud.STRINGS.items():
        print(f"static const char marble_s_{name}[] = \"{hud.rom_str(rom, addr)}\";", file=f)
    print("static const char *const marble_s_races[6] = { "
          + ", ".join(f"\"{hud.rom_str(rom, hud.S_RACES + hud.RACE_STRIDE * k)}\""
                      for k in range(6)) + " };", file=f)
    print("static const char *const marble_s_names[6] = { "
          + ", ".join(f"\"{hud.rom_str(rom, a)}\"" for a in hud.S_NAMES) + " };", file=f)
    for name, (top, bot) in hud.BIG.items():
        ident = "".join(ch for ch in name.lower() if ch.isalpha())
        print(f"static const uint16_t marble_big_{ident}[2][{len(top)}] = {{ "
              f"{{ {', '.join(f'0x{c:03x}' for c in top)} }}, "
              f"{{ {', '.join(f'0x{c:03x}' for c in bot)} }} }};", file=f)
    size3b["glyphs"] = 16 * len(rows)
    size3b["glyph map"] = hud.CODES
    size3b["palettes"] = 2 * (32 + 16)


def particle_palettes(rom):
    """RGB565 pens 0-5 of the particle colours: title sets 0/1 (palette set 0), palette fill
    variants per set (0x26cfa)."""
    rgb = lambda w: rgb888_to_rgb565_le(*md.irgb_hw(w))
    raw = att.title_mo_palette(rom)
    title = [[0] + [rgb(raw[0x100 + cs * 16 + pen]) for pen in range(1, 6)] for cs in range(2)]
    fill = []
    for cs in range(8):
        variants = []
        for v in range(2):
            pal = att.fill_palette(rom, [v] * 8)
            variants.append([0] + [rgb(pal[0x100 + cs * 16 + pen]) for pen in range(1, 6)])
        fill.append(variants)
    return title, fill


def emit_sound(f, size):
    """Phase 5 (MARBLE.md "Sound"): the C64 tune with jingle / effect songs, command table."""
    msid.selfcheck()
    for p, what in ((SOUND_ZIP, "tools/mame_sound.lua capture"),
                    (SID_FILE, "HVSC GAMES/M-R/Marble_Madness.sid")):
        if not os.path.exists(p):
            fatal(f"missing {os.path.abspath(p)} ({what})")
    sid, table, silent, stub, alias, ram, index, songs = msid.build(open(SID_FILE, "rb").read(), SOUND_ZIP)
    print("static const uint8_t MARBLE_SND_IGNORE = 0, MARBLE_SND_SONG = 1, MARBLE_SND_EFFECT = 2;", file=f)
    print("struct MarbleSoundCmd { uint8_t kind; uint8_t arg; };", file=f)
    print("static const MarbleSoundCmd marble_sound_cmds[128] = {\n  "
          + ", ".join(f"{{ {k}, {a} }}" for k, a in table) + "\n};", file=f)
    print(f"static const uint8_t MARBLE_SID_SILENT = {silent};", file=f)
    print(f"static const uint16_t MARBLE_SID_FX_STUB = 0x{stub:04x};", file=f)
    print(f"static const uint16_t MARBLE_SID_ALIAS = 0x{alias:04x};", file=f)
    print(f"static const uint16_t MARBLE_SID_RAM = 0x{ram:04x};", file=f)
    print(f"static const uint8_t MARBLE_SID_SONGS = {struct.unpack('>H', sid[0x0e:0x10])[0]};", file=f)
    print(f"static const uint16_t MARBLE_SID_SIZE = {len(sid)};", file=f)
    emit_compressed(f, "marble_sid", "unsigned char", "", len(sid), list(sid))
    size["sound commands"] = 256
    size["sid"] = len(lzma_compress(sid))
    prev = os.path.join(os.environ.get("TMPDIR", "/tmp"), "marble_sid")
    os.makedirs(prev, exist_ok=True)
    open(os.path.join(prev, "marble.sid"), "wb").write(sid)
    os.makedirs(os.path.dirname(SOUND_FIXTURE), exist_ok=True)
    with open(SOUND_FIXTURE, "w") as g:
        print("// Generated by romconv/internal/marble/marble_rom_convert.py (CRCs only).", file=g)
        print(f"static const uint32_t MARBLE_SID_CRC = 0x{zlib.crc32(sid):08x};", file=g)
        print(f"static const uint8_t MARBLE_SID_EFFECTS = {len(index)};", file=g)
        print(f"static const uint8_t MARBLE_FX_31 = {index[0x31]};", file=g)
        print(f"static const uint8_t MARBLE_SONG_3F = {songs[0x3f]};", file=g)


def emit_attract(f, rom, size6a):
    print("\n// ---- Phase 6a: title (MARBLE.md \"Attract\", tools/marble_attract.py)", file=f)
    print("// High score table defaults (ROM 0x1eea0), table order", file=f)
    print("struct MarbleHiscore { char initials[4]; uint32_t score; };", file=f)
    print("static const MarbleHiscore marble_hiscore_defaults[10] = {", file=f)
    for ini, sc in att.defaults(rom):
        print(f"  {{ \"{ini}\", {sc} }},", file=f)
    print("};", file=f)
    title, fill = particle_palettes(rom)
    print("// Particle colours (MO codes mode | 1, mode | 3): pens 0-5 RGB565; title mode words",
          file=f)
    print("// 0/1 (palette set 0), final screen sets 0-7 x palette fill variant (0x26cfa)", file=f)
    print("static const uint16_t marble_part_title_pal[2][6] = {", file=f)
    for p in title:
        print("  { " + ", ".join(f"0x{v:04x}" for v in p) + " },", file=f)
    print("};", file=f)
    print("static const uint16_t marble_part_fill_pal[8][2][6] = {", file=f)
    for vs in fill:
        print("  { " + ", ".join("{ " + ", ".join(f"0x{v:04x}" for v in p) + " }" for p in vs)
              + " },", file=f)
    print("};", file=f)
    size6a["hiscore defaults"] = 80
    size6a["particle palettes"] = 2 * (len(title) * 6 + 16 * 6)
    size6a["particle tiles"] = 2 * 64


def emit_demo(f, rom, size6b):
    print("\n// ---- Phase 6b: attract demo (MARBLE.md \"Demo race\", tools/marble_demo.py)", file=f)
    print("// Banners: string chain (0x242aa) lines in arcade alpha rows/columns, colour 5 opaque",
          file=f)
    print("struct MarbleBannerLine { uint8_t row, col; const char *text; };", file=f)
    print("struct MarbleBanner { uint32_t chain; uint8_t first, count; };", file=f)
    lines, banners = [], []
    for b in range(7):
        chain = md.u32(rom, hud.BANNERS + 4 * b)
        ls = hud.chain(rom, chain)
        banners.append((chain, len(lines), len(ls)))
        lines += ls
    print(f"static const MarbleBannerLine marble_banner_lines[{len(lines)}] = {{", file=f)
    for row, col, t in lines:
        print(f"  {{ {row}, {col}, \"{t}\" }},", file=f)
    print("};", file=f)
    print(f"static const MarbleBanner marble_banners[{len(banners)}] = {{", file=f)
    print("  " + ", ".join(f"{{ 0x{c:06x}, {a}, {n} }}" for c, a, n in banners) + "\n};", file=f)
    print(f"static const uint8_t MARBLE_BANNER_W = {hud.BANNER_W};", file=f)
    size6b["banners"] = len(lines) * (8 + hud.BANNER_W + 1) + len(banners) * 8
    size6b["waypoints"] = 8 + 0x88 + 0x1c


def write_attract_fixture(rom, title):
    crc = lambda fmt, v: zlib.crc32(struct.pack(fmt, *v))
    rows, tiles, mapw, pal = title
    tpal, fill = particle_palettes(rom)
    hs = b"".join(ini.encode() + b"\0" + struct.pack("<I", sc) for ini, sc in att.defaults(rom))
    part = b"".join(md.mo_tile(rom, md.mo_lookup(rom, c)[0]) for c in (1, 2, 3, 4))
    with open(ATTRACT_FIXTURE, "w") as f:
        print("// Generated by romconv/internal/marble/marble_rom_convert.py; do not edit.", file=f)
        print("// Counts and CRC32s only, no ROM data (tools/marble_attract.py).", file=f)
        print(f"static const uint16_t MARBLE_ATT_FIX_TITLE_ROWS = {rows};", file=f)
        print(f"static const uint16_t MARBLE_ATT_FIX_TITLE_TILES = {len(tiles)};", file=f)
        print(f"static const uint32_t MARBLE_ATT_FIX_MAP_CRC = 0x{crc(f'<{len(mapw)}H', mapw):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_ATT_FIX_TILES_CRC = 0x{zlib.crc32(bytes(p for t in tiles for p in t)):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_ATT_FIX_PAL_CRC = 0x{crc('<32H', pal[:32]):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_ATT_FIX_HISCORE_CRC = 0x{zlib.crc32(hs):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_ATT_FIX_TITLE_PART_CRC = 0x{crc('<12H', [v for p in tpal for v in p]):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_ATT_FIX_FILL_CRC = 0x{crc('<96H', [v for vs in fill for p in vs for v in p]):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_ATT_FIX_PART_TILES_CRC = 0x{zlib.crc32(part):08x}u;", file=f)


def write_hud_fixture(rom):
    codes, rows, gmap, pal, pulse, _ = hud_data(rom)
    crc = lambda fmt, v: zlib.crc32(struct.pack(fmt, *v))
    s = hud.font()[ord("S")]
    with open(HUD_FIXTURE, "w") as f:
        print("// Generated by romconv/internal/marble/marble_rom_convert.py; do not edit.", file=f)
        print("// Counts and CRC32s only, no ROM data (tools/marble_hud.py).", file=f)
        print(f"static const uint32_t MARBLE_HUD_FIX_GLYPHS = {len(rows)};", file=f)
        print(f"static const uint32_t MARBLE_HUD_FIX_GLYPH_CRC = 0x{crc(f'<{8 * len(rows)}H', [v for g in rows for v in g]):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_HUD_FIX_MAP_CRC = 0x{crc(f'{len(gmap)}B', gmap):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_HUD_FIX_PAL_CRC = 0x{crc('<32H', pal):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_HUD_FIX_PULSE_CRC = 0x{crc('<16H', pulse):08x}u;", file=f)
        rgb = lambda w: rgb888_to_rgb565_le(*md.irgb_hw(w))
        print("// remake layout cases (tools/marble_hud.py LAYOUT_CASES): HUD state, CRC32 of the",
              file=f)
        print("// 36 x 30 grid words and of the 32 RGB565 colours", file=f)
        print("// stage: title frames since its start, tcase: title table (0 ROM defaults, 1 entry",
              file=f)
        print("// 1 = blank initials 9,999,999)", file=f)
        print("struct MarbleHudCase { uint16_t screen; uint32_t score; int32_t value; int16_t seconds;",
              file=f)
        print("  uint8_t race, add, deaths, pal3, pal11, stage, tcase, banner; uint32_t grid_crc, pal_crc; };",
              file=f)
        print("static const MarbleHudCase MARBLE_HUD_FIX_CASES[] = {", file=f)
        for c, h in zip(hud.LAYOUT_CASES, hud.layout_states(rom)):
            g = [w for row in hud.view(h, rom) for w in row]
            pal = [rgb(w) for w in hud.palette(h, rom)]
            c = tuple(getattr(h, n) for n in hud.LAYOUT_FIELDS)
            print("  { " + ", ".join(str(v) for v in c) + f", 0x{crc(f'<{len(g)}H', g):08x}u, "
                  f"0x{crc('<32H', pal):08x}u }},", file=f)
        print("};", file=f)
        pens = [p for line in s for p in line]
        print(f"static const uint32_t MARBLE_HUD_FIX_S_CRC = 0x{crc('64B', pens):08x}u;  // pens of 'S'", file=f)


def emit_death(f, rom, size4b, size4b2, size4b3, size4b4, size4b5, size4b6):
    """Phase 4b-0: player death sprites of every race (MARBLE.md "Phase 4b-0 data"): tile set
    (sparkle stars included), frame table, palettes of sets 0 and 2. Phase 4b-2: the slinky
    frames and their palette (set 4) join it, Phase 4b-3 the acid frames (set 6), Phase 4b-4a
    the silly race bird frames (set 7), Phase 4b-4b the minis, Phase 4b-5 the aerial
    pistons (aerial set 4)."""
    ds = DEATH
    sprite_check_codes(rom, ds, DEATH_SETS + (STEELIE_SET, SLINKY_SET, ACID_SET, BIRD_SET)
                       + MINI_SETS + (PISTON_SET,))
    first = ds.tile_of[mo.STAR_CODE]
    if any(ds.tile_of[mo.STAR_CODE + k] != first + k for k in range(mo.STARS)):
        fatal("marble: sparkle star tiles not consecutive")
    tiles = sprite_set(rom, ds)
    print("\n// ---- Phase 4b-0: player death sprites (MARBLE.md \"Phase 4b-0 data\")", file=f)
    for line in (
            "// Death set, every race: frames of the player animation lists (dizzy, eaten, shatter,",
            "// dissolve, rl(rl(+0x5a))) and the shatter overlay (rl(rl(+0x62))), ids as the race",
            "// sets (header frames only, no strip). Its MARBLE_DEATH_TILES tiles open the sprite",
            "// tile buffer; columns index them directly. Sparkle star MO code MARBLE_STAR_CODE + k",
            "// (k < MARBLE_STARS, height 1, 0x13ade) is buffer tile MARBLE_STAR_TILE + k."):
        print(line, file=f)
    pool = frame_table(f, "death", ds, 0)
    emit_compressed(f, "marble_death_tiles", "unsigned char", "[32]", len(ds.tile_codes),
                    list(tiles))
    print(f"static const uint16_t MARBLE_DEATH_TILES = {len(ds.tile_codes)};", file=f)
    enemy = {h for a in mo.SLINKY_LISTS + mo.ACID_LISTS + mo.BIRD_LISTS + mo.MINI_LISTS
             + mo.PISTON_LISTS for h in mo.anim_list(a)}
    early = {h for a in mo.PISTON_LISTS for h in mo.anim_list(a)}
    nfirst = len(early)
    nplayer = len(ds.hdrs) - len(enemy)
    if any(ds.ids[h] >= nfirst for h in early) or \
            any(nfirst <= ds.ids[h] < nfirst + nplayer for h in enemy):
        fatal("marble: death set not pistons, player frames, other enemies")
    print("// Frame ids MARBLE_DEATH_FIRST .. + MARBLE_DEATH_FRAMES are player death frames; the",
          file=f)
    print("// piston frames come before them, the other enemy frames (acid, slinkies, birds,", file=f)
    print("// minis) after them (ids by header address).", file=f)
    print(f"static const uint8_t MARBLE_DEATH_FIRST = {nfirst};", file=f)
    print(f"static const uint8_t MARBLE_DEATH_FRAMES = {nplayer};", file=f)
    print(f"static const uint16_t MARBLE_STAR_CODE = 0x{mo.STAR_CODE:03x};", file=f)
    print(f"static const uint8_t MARBLE_STARS = {mo.STARS};", file=f)
    print(f"static const uint16_t MARBLE_STAR_TILE = {first};", file=f)
    print(f"static const MarbleSpriteSet marble_death_set = {{ marble_sprite_frames_death, "
          f"marble_sprite_cols_death, marble_sprite_addr_death, {len(ds.hdrs)}, {len(ds.cols)}, "
          f"{len(ds.tile_codes)} }};", file=f)
    pals = {tuple(tuple(rgb888_to_rgb565_le(*md.palette(rom, n, True)[0x100 + st * 16 + pen])
                        for pen in range(8)) for st in DEATH_SETS) for n in range(md.LEVELS)}
    if len(pals) != 1:
        fatal("marble: death frame palettes differ between races")
    slinky = race_pal(rom, SLINKY_SET, SLINKY_RACES, "slinky")
    acid = race_pal(rom, ACID_SET, ACID_RACES, "acid")
    bird = race_pal(rom, BIRD_SET, BIRD_RACES, "bird")
    minis = tuple(race_pal(rom, st, BIRD_RACES, "mini") for st in MINI_SETS)
    piston = race_pal(rom, PISTON_SET, PISTON_RACES, "piston")
    print("// Palette of the death set frames, set 0 (player frames, stars), set 2 (shatter",
          file=f)
    print("// overlay), set 4 of the slinky races (slinkies), set 6 of the acid races (acid), set",
          file=f)
    print("// 7 of the silly race (birds), sets 5, 6, 4 of the silly race (minis of type 7, 8, 9),",
          file=f)
    print("// set 4 of the aerial race (pistons), pens 0-7 as RGB565 byte swapped.", file=f)
    print("static const uint16_t marble_death_pal[9][8] = {", file=f)
    for row in pals.pop() + (slinky, acid, bird) + minis + (piston,):
        print("  {" + ",".join(f"0x{v:04x}" for v in row) + "},", file=f)
    print("};", file=f)
    packed = len(lzma_compress(tiles))
    print(f"death set: {len(ds.hdrs)} frame headers, {len(ds.tile_codes)} tiles (stars at "
          f"{first}), {len(tiles)} B raw (Arena), {packed} B LZMA, {len(pool)} columns "
          f"(of {sum(len(c) for c in ds.cols)})")
    plain = mo.DeathSprites(slinkies=False, acid=False, birds=False, minis=False, pistons=False)
    packed0 = len(lzma_compress(sprite_set(rom, plain)))
    pool0 = {tuple(plain.objs_cols(fid)) for fid in range(len(plain.cols))}
    cols0 = 6 * sum(len(c) for c in pool0)
    sls = mo.DeathSprites(acid=False, birds=False, minis=False, pistons=False)
    bds = mo.DeathSprites(minis=False, pistons=False)
    mns = mo.DeathSprites(pistons=False)
    packed4 = len(lzma_compress(sprite_set(rom, mns)))
    cols4 = 6 * sum(len(c) for c in {tuple(mns.objs_cols(fid)) for fid in range(len(mns.cols))})
    packed3 = len(lzma_compress(sprite_set(rom, bds)))
    cols3 = 6 * sum(len(c) for c in {tuple(bds.objs_cols(fid)) for fid in range(len(bds.cols))})
    packed1 = len(lzma_compress(sprite_set(rom, sls)))
    cols1 = 6 * sum(len(c) for c in {tuple(sls.objs_cols(fid)) for fid in range(len(sls.cols))})
    acs = mo.DeathSprites(birds=False, minis=False, pistons=False)
    packed2 = len(lzma_compress(sprite_set(rom, acs)))
    cols2 = 6 * sum(len(c) for c in {tuple(acs.objs_cols(fid)) for fid in range(len(acs.cols))})
    size4b.update({"tiles": packed0 + 16, "frames": 4 * len(plain.cols), "columns": cols0,
                   "header ids": 2 * len(plain.hdrs), "palette": 2 * 16, "set": 16})
    size4b2.update({"slinky tiles": packed1 - packed0,
                    "slinky frames": 4 * (len(sls.cols) - len(plain.cols)),
                    "slinky columns": cols1 - cols0,
                    "slinky header ids": 2 * (len(sls.hdrs) - len(plain.hdrs)),
                    "slinky palette": 16})
    size4b3.update({"acid tiles": packed2 - packed1,
                    "acid frames": 4 * (len(acs.cols) - len(sls.cols)),
                    "acid columns": cols2 - cols1,
                    "acid header ids": 2 * (len(acs.hdrs) - len(sls.hdrs)),
                    "acid palette": 16})
    size4b4.update({"bird tiles": packed3 - packed2,
                    "bird frames": 4 * (len(bds.cols) - len(acs.cols)),
                    "bird columns": cols3 - cols2,
                    "bird header ids": 2 * (len(bds.hdrs) - len(acs.hdrs)),
                    "bird palette": 16})
    size4b5.update({"mini tiles": packed4 - packed3,
                    "mini frames": 4 * (len(mns.cols) - len(bds.cols)),
                    "mini columns": cols4 - cols3,
                    "mini header ids": 2 * (len(mns.hdrs) - len(bds.hdrs)),
                    "mini palettes": 3 * 16})
    size4b6.update({"piston tiles": packed - packed4,
                    "piston frames": 4 * (len(ds.cols) - len(mns.cols)),
                    "piston columns": 6 * len(pool) - cols4,
                    "piston header ids": 2 * (len(ds.hdrs) - len(mns.hdrs)),
                    "piston palette": 16})


def main():
    global DEATH
    rom = md.load()
    levels = [convert_level(rom, n) for n in range(md.LEVELS)]
    title = convert_title(rom)
    if max(len(l[1]) for l in levels) > MAX_TILES:
        fatal(f"marble: a tile set exceeds MAX_TILES {MAX_TILES}")
    if max(l[0] for l in levels) != MAX_ROWS or max(len(l[1]) for l in levels) != MAX_TILES:
        fatal("MAX_ROWS / MAX_TILES changed, update marble_level.h and this file")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        print("// Generated by romconv/internal/marble/marble_rom_convert.py; do not edit.", file=f)
        print("#ifndef MARBLE_ROM_H\n#define MARBLE_ROM_H\n#include <stdint.h>", file=f)
        print("struct MarbleLevelInfo { uint16_t rows; uint16_t tiles; };", file=f)
        print(f"static const uint8_t MARBLE_TITLE_LEVEL = {md.LEVELS};  // the title playfield",
              file=f)
        print("static const MarbleLevelInfo marble_levels[7] = {", file=f)
        print("  " + ", ".join(f"{{ {r}, {len(t)} }}" for r, t, _, _ in levels + [title])
              + "\n};", file=f)
        for n, (rows, tiles, mapw, _) in enumerate(levels + [title]):
            emit_compressed(f, f"marble_l{n}_map", "unsigned short", "", len(mapw), mapw)
            emit_compressed(f, f"marble_l{n}_tiles", "unsigned char", "[64]", len(tiles),
                            [p for t in tiles for p in t])
        print("static const FlashAsset<unsigned short, COMPRESSED> marble_maps[7] = { "
              + ", ".join(f"marble_l{n}_map_blob" for n in range(md.LEVELS + 1)) + " };", file=f)
        print("static const FlashAsset<unsigned char[64], COMPRESSED> marble_tiles[7] = { "
              + ", ".join(f"marble_l{n}_tiles_blob" for n in range(md.LEVELS + 1)) + " };", file=f)
        print("static const unsigned short marble_pf_palette[7][128] = {", file=f)
        for _, _, _, pal in levels + [title]:
            print("  { " + ", ".join(f"0x{v:04x}" for v in pal) + " },", file=f)
        print("};", file=f)
        size3, size4, size4b, size4b1, size4b2, size4b3, size4b4, size4b5, size4b6 = \
            {}, {}, {}, {}, {}, {}, {}, {}, {}
        SPRITES[:] = [mo.Sprites(n) for n in range(md.LEVELS)]
        DEATH = mo.DeathSprites()
        size = emit_physics(f, rom, size3, size4, size4b, size4b1, size4b2, size4b3, size4b4,
                            size4b5, size4b6)
        emit_objects(f, rom, levels, size3)
        emit_sprites(f, rom, size4, size4b1, size4b2)
        emit_death(f, rom, size4b, size4b2, size4b3, size4b4, size4b5, size4b6)
        size3b = {}
        emit_hud(f, rom, size3b)
        size6a = {"title map + tiles": len(lzma_compress(struct.pack(f"<{len(title[2])}H", *title[2])))
                  + len(lzma_compress(bytes(p for t in title[1] for p in t))), "title palette": 256}
        emit_attract(f, rom, size6a)
        size6b = {}
        emit_demo(f, rom, size6b)
        size5 = {}
        emit_sound(f, size5)
        print("#endif", file=f)
    write_hud_fixture(rom)
    write_attract_fixture(rom, title)
    print("phase 6a (title) flash: " + ", ".join(f"{k} {v}" for k, v in size6a.items())
          + f", total {sum(size6a.values())} B")
    print("phase 6b (demo) flash: " + ", ".join(f"{k} {v}" for k, v in size6b.items())
          + f", total {sum(size6b.values())} B")
    total5 = sum(size5.values())
    print("phase 5 (sound) flash: " + ", ".join(f"{k} {v}" for k, v in size5.items())
          + f", total {total5} B")
    if total5 > SOUND_BUDGET:
        fatal(f"marble: phase 5 data {total5} B over the {SOUND_BUDGET} B budget")
    total = sum(size.values())
    print("phase 2 flash: " + ", ".join(f"{k} {v}" for k, v in size.items()) + f", total {total} B")
    if total > FLASH_BUDGET:
        fatal(f"marble: phase 2 data {total} B over the {FLASH_BUDGET} B budget")
    total3 = sum(size3.values())
    print("phase 3 flash: " + ", ".join(f"{k} {v}" for k, v in size3.items())
          + f", total {total3} B")
    if total3 > PHASE3_BUDGET:
        fatal(f"marble: phase 3 data {total3} B over the {PHASE3_BUDGET} B budget")
    total4 = sum(size4.values())
    print("phase 4a flash: " + ", ".join(f"{k} {v}" for k, v in size4.items())
          + f", total {total4} B")
    print("phase 3b (HUD) flash: " + ", ".join(f"{k} {v}" for k, v in size3b.items())
          + f", total {sum(size3b.values())} B")
    total4b = sum(size4b.values())
    print("phase 4b-0 flash: " + ", ".join(f"{k} {v}" for k, v in size4b.items())
          + f", total {total4b} B")
    total4b1 = sum(size4b1.values())
    print("phase 4b-1 flash: " + ", ".join(f"{k} {v}" for k, v in size4b1.items())
          + f", total {total4b1} B")
    total4b2 = sum(size4b2.values())
    print("phase 4b-2 flash: " + ", ".join(f"{k} {v}" for k, v in size4b2.items())
          + f", total {total4b2} B")
    total4b3 = sum(size4b3.values())
    print("phase 4b-3 flash: " + ", ".join(f"{k} {v}" for k, v in size4b3.items())
          + f", total {total4b3} B")
    total4b4 = sum(size4b4.values())
    print("phase 4b-4a (birds, silly tubes) flash: " + ", ".join(f"{k} {v}" for k, v in size4b4.items())
          + f", total {total4b4} B")
    total4b5 = sum(size4b5.values())
    print("phase 4b-4b (minis) flash: " + ", ".join(f"{k} {v}" for k, v in size4b5.items())
          + f", total {total4b5} B")
    total4b6 = sum(size4b6.values())
    print("phase 4b-5 (pistons) flash: " + ", ".join(f"{k} {v}" for k, v in size4b6.items())
          + f", total {total4b6} B")
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
        print(f"// largest published object count of the draw list sweep (marble_mo.race_sweep: step "
              f"{mo.SWEEP_STEP}, margin {mo.SWEEP_MARGIN}), per race", file=f)
        print("static const uint8_t MARBLE_FIX_SWEEP_MAX[6] = { "
              + ", ".join(str(v) for v in SWEEP_MAX) + " };", file=f)
        print("static const uint32_t MARBLE_FIX_FRAME_CRC[6][3] = {", file=f)
        for rows, tiles, mapw, pal in levels:
            c = [frame_crc(rows, tiles, mapw, pal, sx, 0) for sx in (0, PAN // 2, PAN)]
            print("  { " + ", ".join(f"0x{v:08x}u" for v in c) + " },", file=f)
        print("};", file=f)
    for n, (rows, tiles, _, _) in enumerate(levels):
        print(f"level {n} {md.NAMES[n]}: {rows} rows, {len(tiles)} tiles")


if __name__ == "__main__":
    main()
