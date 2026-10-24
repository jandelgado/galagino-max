"""Marble Madness: per-level playfield tiles, maps and palettes for the galagino remake.

Each level gets its own tile set (only the tiles its map uses, in first-use order) so one
level fits fixed Arena buffers (MAX_TILES x 64 + MAX_ROWS x 36 x 2 bytes). Map word:
bits 0-9 local tile, 10-11 colour set, 15 hflip. Also writes the host test fixture.

Phase 2 (marble physics, MARBLE.md "Remake data"): terrain per race, the program ROM
tables the physics reads (progrom.py), marble motion object tiles and palette.

Phase 3 (level objects and occlusion): more program ROM tables (slot scripts, animation
lists, terrain blocks, occlusion tables; edge bitmaps become edge lists), practice gate
playfield patches (tiles added to the practice tile set), occlusion mask tiles.

Phase 4a (level object sprites) and 4b-0 (player death sprites, shared by every race; their
tiles open the sprite tile buffer, the race set's follow).

The state of a conversion is the Run object passed to the functions: the ROM the simulator
reads, the sprite sets, the table pool and the files to write. Nothing is kept at module level.
"""
import io
import struct
import zlib
from contextlib import contextmanager
from functools import partial
from dataclasses import dataclass, field

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.emit.asset import emit_compressed, emit_plain
from romconv.emit.lzma import lzma_compress
from romconv.emit.text import hex_block
from romconv.emit.write import module_path
from romconv.games.marble import data as md
from romconv.games.marble import progrom as pr
from romconv.games.marble import sid as msid
from romconv.games.marble.sim import marble_attract as att
from romconv.games.marble.sim import marble_hud as hud
from romconv.games.marble.sim import marble_mo as mo
from romconv.games.marble.sim import marble_sim as ms
from romconv.games.marble.sim import objects_data as od
from romconv.gfx.color import rgb565_swapped
from romconv.misc.userfile import read_user_file

_SID_FILE = "Marble_Madness.sid"
_SOUND_BUDGET = 48 * 1024  # C64 tune + the arcade sound program (32 KB, plain)

# Host test inputs and outputs: (directory, file) under source/test/. The traces are MAME
# captures that stay tracked (dev/marble/make_fixture.py writes them); the fixtures are written here.
TRACE_FIXTURE = ("test_marble_physics", "marble_trace.h")
OBJ_FIXTURE = ("test_marble_objects", "marble_objects_trace.h")
SPRITE_FIXTURE = ("test_marble_sprites", "marble_sprites_trace.h")
STEELIE_FIXTURE = ("test_marble_steelies", "marble_steelies_trace.h")
ACID_FIXTURE = ("test_marble_acid", "marble_acid_trace.h")
BIRD_FIXTURE = ("test_marble_birds", "marble_birds_trace.h")
TUBE_FIXTURE = ("test_marble_deaths", "marble_tubes_trace.h")
AERIAL_FIXTURE = ("test_marble_aerial", "marble_aerial_trace.h")
MINI_FIXTURE = ("test_marble_minis", "marble_minis_trace.h")
PISTON_FIXTURE = ("test_marble_pistons", "marble_pistons_trace.h")
SLINKY_FIXTURE = ("test_marble_slinkies", "marble_slinkies_trace.h")
DEATH_FIXTURE = ("test_marble_deaths", "marble_deaths_trace.h")
_SOUND_FIXTURE = ("test_marble_sound", "marble_sound_fixture.h")
_LEVEL_FIXTURE = ("test_marble_level", "marble_fixture.h")
_HUD_FIXTURE = ("test_marble_hud", "marble_hud_fixture.h")
_ATTRACT_FIXTURE = ("test_marble_attract", "marble_attract_fixture.h")
TRACE_INPUTS = (TRACE_FIXTURE, OBJ_FIXTURE, SPRITE_FIXTURE, STEELIE_FIXTURE, ACID_FIXTURE, BIRD_FIXTURE,
                TUBE_FIXTURE, AERIAL_FIXTURE, MINI_FIXTURE, PISTON_FIXTURE, SLINKY_FIXTURE, DEATH_FIXTURE)
OUTPUT_FIXTURES = (_SOUND_FIXTURE, _LEVEL_FIXTURE, _HUD_FIXTURE, _ATTRACT_FIXTURE)


@dataclass


class Run:
    """One conversion: inputs, the simulator's ROM and what the stages accumulate."""
    ctx: Context
    rom: md.Rom             # decoded ROM set
    sim: ms.SimRom          # the program ROM the simulator reads
    sprites: list = field(default_factory=list)       # marble_mo.Sprites per race (full ROM)
    death: mo.DeathSprites | None = None              # built from the full ROM
    sweep_max: list = field(default_factory=lambda: [0] * md.LEVELS)   # largest published object
    #                                                   count of the race sweep, per race
    patch_words: dict = field(default_factory=dict)   # (race, block address) -> map words (local
    #                                                   tile | colour set << 10 | flip)
    tables: bytearray = field(default_factory=bytearray)   # marble_tables: emitted last, LZMA packed
    outputs: dict = field(default_factory=dict)       # host test fixtures: path -> text, written last

    def test_path(self, spec):
        """Path of a host test file (directory, name) under source/test."""
        return self.ctx.paths.test / spec[0] / spec[1]

    @contextmanager
    def fixture(self, spec):
        """Text sink for a generated host test file (written when the conversion succeeded)."""
        buffer = io.StringIO()
        yield buffer
        self.outputs[self.test_path(spec)] = buffer.getvalue()

    def trace(self, spec, hint):
        """Path of a tracked MAME trace fixture, which must exist."""
        path = self.test_path(spec)
        if not path.exists():
            raise ConversionError(f"marble: {path} missing, run uv run python -m dev.marble.make_fixture {hint}")
        return path


_MAX_ROWS = 145   # must match marble_level.h
_MAX_TILES = 896  # must match marble_level.h
_VIEW_W, _VIEW_H, _PAN = 240, 288, 48


def _convert_level(run, n):
    rom = run.rom
    _words = md.decode_level(rom, n)
    local = {}
    tiles = []
    mapw = []

    def tile_word(w):
        code, cs, flip = md.pf_lookup(rom, w)
        if code not in local:
            local[code] = len(tiles)
            tiles.append(md.pf_tile(rom, code))
            if len(tiles) > 1024:
                raise ConversionError("marble: more than 1024 tiles in a level (map word has 10 tile bits)")
        if cs > 3:
            raise ConversionError(f"marble: colour set {cs} > 3 (map word has 2 colour bits)")
        return local[code] | cs << 10 | (0x8000 if flip else 0)

    for row in _words:
        for w in row:
            mapw.append(tile_word(w))
    # playfield patches of the level objects: their tiles join the tile set
    for race, rec, col, prow, w, h, blk in od.patches(run.sim):
        if race != n or (n, blk) in run.patch_words: continue
        run.patch_words[(n, blk)] = [tile_word(md.u16(rom, blk + 2 + 2 * k)) for k in range(w * h)]
        if len(tiles) > _MAX_TILES:
            raise ConversionError(f"marble: race {n} patch tiles raise the tile set to {len(tiles)} > "
                  f"MAX_TILES {_MAX_TILES}")
    rgb = md.palette(rom, n, True)[md.PF_PALETTE:md.PF_PALETTE + 128]
    pal = [rgb565_swapped(*c) for c in rgb]
    return len(_words), tiles, mapw, pal


_TITLE_ROWS = 36   # title playfield: one screen, logo at rows 5-10 (MARBLE.md "Attract")
_TITLE_TOP, _TITLE_LEFT = 5, 8


def _convert_title(run):
    """Title playfield as a level: the logo words (marble_attract.LOGO) in a black map; ROM
    tiles, playfield set 0 = the logo palette."""
    rom = run.rom
    local, tiles, mapw = {}, [], []

    def tile_word(w):
        code, cs, flip = md.pf_lookup(rom, w)
        if cs:
            raise ConversionError(f"marble: title word {w:04x} uses colour set {cs}")
        if code not in local:
            local[code] = len(tiles)
            tiles.append(md.pf_tile(rom, code))
        return local[code] | (0x8000 if flip else 0)

    blank = tile_word(0)
    if any(tiles[blank & 0x3ff]):
        raise ConversionError("marble: playfield word 0 is not a blank tile")
    for row in range(_TITLE_ROWS):
        for col in range(md.COLS):
            y, x = row - _TITLE_TOP, col - _TITLE_LEFT
            inside = 0 <= y < len(att.LOGO) and 0 <= x < len(att.LOGO[0])
            mapw.append(tile_word(att.LOGO[y][x]) if inside else blank)
    rgb = [md.irgb_hw(w) for w in att.logo_palette(rom)]
    pal = [rgb565_swapped(*c) for c in rgb] + [0] * 96
    return _TITLE_ROWS, tiles, mapw, pal


def _frame_crc(rows, tiles, mapw, pal, sx, sy):
    """Reference for marble_video::renderRow, 240x288 frame as little-endian uint16."""
    out = bytearray()
    for y in range(sy, sy + _VIEW_H):
        for x in range(sx, sx + _VIEW_W):
            w = mapw[(y >> 3) * md.COLS + (x >> 3)]
            c = x & 7
            if w & 0x8000:
                c = 7 - c
            p = tiles[w & 0x3FF][(y & 7) * 8 + c]
            out += struct.pack("<H", pal[((w >> 10) & 3) * 32 + p])
    return zlib.crc32(out)


# ---------------------------------------------------------------- Phase 2: marble physics

_FLASH_BUDGET = 50 * 1024
_PHASE3_BUDGET = 25 * 1024
_PAIR_BYTES = 54   # two diagonals, 44 x 10 bit fields MSB first, last 8 bits not stored
_DESC_TOP, _DESC_RANGE, _DESC_CELL = 0x10, 0x12, 0x14   # descriptor words: camera top, range, start cell
_MO_CODES = range(0x01, 0x2e)  # particle half 0x01-0x02, marble body 0x03-0x06, 0x07, dots 0x0d-0x2d


def terrain_race(rom_terrain):
    """Terrain of one race as emitted: index bytes, codes, records bytes, bases."""
    t = rom_terrain
    if t.maxd & 1:
        raise ConversionError(f"marble: race {t.race} has an odd diagonal count")
    # pair p as written to video RAM 0x1eb3a[p] (validated against MAME: marble_sim.py terrain)
    img = t.vram_image()
    index = b"".join(img[ms.s16(t.rom.w(0x1eb3a + 2 * p))] for p in range(t.maxd // 2))
    rd = ms.Rd(t.rom, t.race)
    rd.bank = t.bank
    recs = rd.l(t.desc)
    end = recs
    while rd.l(end) != 0xFFFFFFFF:
        end += 4
    records = bytes(rd.b(a) for a in range(recs, end + 4))
    used = {t.codes[i] for d in t.diag for i in d}
    for c in used:
        if 0 < c < 0x800 and c + 4 > len(records):
            raise ConversionError(f"marble: race {t.race} record code {c:04x} beyond its records")
    _check_dynamic(t, used, len(records))
    return bytes(index), t.codes, records, recs, t.bases


def _check_dynamic(t, used, nrecords):
    """Dynamic codes 0x0800-0x0fff the terrain uses: slot set by dyn_init, value a record
    code within the records or a non-dynamic code (0 = not initialised: no floor)."""
    m = ms.Mem(t, bytes(0x2000))
    ms.dyn_init(m, t.race)
    for c in sorted(c for c in used if 0x800 <= c < 0x1000):
        v = m.rw(ms.DYN + (c & 0x7FE))
        if v == 0:
            raise ConversionError(f"marble: race {t.race} dynamic code {c:04x} not initialised by dyn_init")
        if 0x800 <= v < 0x1000:
            raise ConversionError(f"marble: race {t.race} dynamic code {c:04x} maps to dynamic code {v:04x}")
        if v < 0x800 and v + 4 > nrecords:
            raise ConversionError(f"marble: race {t.race} dynamic code {c:04x} -> record {v:04x} beyond records")


def edge_tables(run):
    """Terrain edge diagonals of 0x264aa. Per race and descriptor table (4 for the ultimate
    race, one per scroll frame) and column 0-35: the ascending diagonals < maxd with a bit in
    the edge bitmap at descriptor +0x2e. Returns [(race, [36 lists])] per table, checks that
    the list lookup equals the bitmap scan (with the remake guard) for every d < maxd."""
    sim = run.sim
    out = []
    for race in range(md.LEVELS):
        desc = sim.l(0x2be00 + 4 * race)
        maxd = sim.w(desc + 0x18)
        for k in range(4 if race == 5 else 1):
            cols = []
            for c in range(36):
                bm = sim.l(desc + 0x2e + 0x90 * k + 4 * c)
                lst = [d for d in range(maxd) if sim.prog[bm + (d >> 3)] >> (d & 7) & 1]
                if not lst or lst[-1] != maxd - 1 or maxd > 256:
                    raise ConversionError(f"marble: race {race} column {c}: edge list unusable")
                cols.append(lst)
            out.append((race, cols))
    return out


def edge_lookup(tables, race, ult, c, d):
    """First edge diagonal >= d of column c (-1: none), as the remake reads the lists."""
    base = sum(1 for r, _ in tables if r < race)
    for e in tables[base + ult // 0x90][1][c]:
        if e >= d: return e
    return -1


def _edge_check(run, tables):
    """List lookup == arcade bitmap scan (0x264aa, first set bit at or after d) for all d."""
    sim = run.sim
    for race in range(md.LEVELS):
        desc = sim.l(0x2be00 + 4 * race)
        maxd = sim.w(desc + 0x18)
        for k in range(4 if race == 5 else 1):
            for c in range(36):
                bm = sim.l(desc + 0x2e + 0x90 * k + 4 * c)
                for d in range(maxd + 1):
                    want = next((e for e in range(d, maxd)
                                 if sim.prog[bm + (e >> 3)] >> (e & 7) & 1), -1)
                    if edge_lookup(tables, race, 0x90 * k, c, d) != want:
                        raise ConversionError(f"marble: edge list differs from bitmap: race {race} "
                              f"table {k} column {c} d {d}")


def race_sweeps(run, mem, terrains):
    """Race start and a camera sweep over the whole race with the draw list on Mem class mem:
    every frame header the sort reads, and the object counts the host test expects
    (run.sweep_max). Returns crc32 of the published lists per race."""
    out = []
    for n in range(md.LEVELS):
        m = mem(terrains[n], bytes(0x2000))
        m.objects = m.sprites = True
        m.drawlist = mo.DrawList()
        ms.start_race(m, n)
        ms.snapshot_start(m)
        run.sweep_max[n], got = mo.race_sweep(m, n, run.sprites[n])
        out.append(zlib.crc32(repr(got).encode()))
    return out


def _mo_sprites(rom):
    """Marble motion object tiles (codes MO_CODES, consecutive) and the 16 colour palette."""
    tiles = []
    sets = set()
    for code in _MO_CODES:
        tile, cs = md.mo_lookup(rom, code)
        tiles.append(md.mo_tile(rom, tile))
        sets.add(cs)
    if len(sets) != 1:
        raise ConversionError("marble: marble motion objects use more than one palette set")
    cs = sets.pop()
    pals = {tuple(md.palette(rom, n, True)[0x100 + cs * 16:0x110 + cs * 16]) for n in range(md.LEVELS)}
    if len(pals) != 1:
        raise ConversionError("marble: motion object palette differs between races")
    return tiles, [rgb565_swapped(*c) for c in pals.pop()]


def _c_bytes(b, indent="  "):
    return ",\n".join(indent + ",".join(f"0x{v:02x}" for v in b[k:k + 24])
                      for k in range(0, len(b), 24))


def _c_words(w, indent="  "):
    return ",\n".join(indent + ",".join(f"0x{v:04x}" for v in w[k:k + 16])
                      for k in range(0, len(w), 16))


def _table_add(run, data, align=2):
    """Appends data to marble_tables, returns its byte offset (align: 1, 2 or 4)."""
    while len(run.tables) % align:
        run.tables.append(0)
    at = len(run.tables)
    run.tables.extend(data)
    if len(run.tables) > 0xffff:
        raise ConversionError("marble: tables too long for 16 bit offsets")
    return at


def _emit_table(run, f, name, ctype, dims, data):
    """Table `name` of ctype elements (inner dims) into marble_tables, read through name()."""
    at = _table_add(run, data)
    ret = f"const {ctype} (*{name}()){dims}" if dims else f"const {ctype} *{name}()"
    print(f"static inline {ret} {{ return marble_table<{ctype}{dims}>({at}); }}", file=f)


def _words(w):
    return struct.pack(f"<{len(w)}H", *w)


def _emit_physics(run, f, rom, size3, size4, size4b, size4b1, size4b2, size4b3, size4b4, size4b5,
                 size4b6):
    """Phase 2 data into marble_rom.h. Returns {item: flash bytes}; the program ROM tables of
    Phase 3 go to size3, of Phase 4a to size4, of Phase 4b-0 to size4b, of 4b-1 to size4b1,
    of 4b-2 to size4b2, of 4b-3 to size4b3, of 4b-4a to size4b4, of 4b-4b to size4b5, of 4b-5
    to size4b6."""
    size = {}
    terrains = [ms.Terrain(run.sim, n) for n in range(md.LEVELS)]
    races = [terrain_race(t) for t in terrains]
    edges = edge_tables(run)
    _edge_check(run, edges)
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
          "  uint16_t codes_at, ncodes;\n  uint16_t records_at;\n"
          "  uint16_t maxd;\n  uint16_t nrecords;\n  int16_t top, range; uint16_t cell;\n"
          "  uint16_t edges_at; uint8_t nedges;\n"
          "  const uint16_t *codes() const { return marble_table<uint16_t>(codes_at); }\n"
          "  const uint8_t *records() const { return marble_table<uint8_t>(records_at); }\n"
          "  const uint16_t *edges() const { return marble_table<uint16_t>(edges_at); }\n};",
          file=f)
    # edge lists: marble_edge_lists[marble_edge_off[col]...] of the table of race and scroll
    # frame, first diagonal >= d (the last list entry is maxd - 1); equal lists are shared
    pool, where, offs = bytearray(), {}, {}
    for t, (race, cols) in enumerate(edges):
        row = []
        for lst in cols:
            key = bytes(lst)
            if key not in where:
                where[key] = len(pool); pool += key
            row.append(where[key])
        offs.setdefault(race, []).append(row)
    if len(pool) > 0xffff:
        raise ConversionError("marble: edge lists too long for 16 bit offsets")
    k = {}   # read back what is emitted: every list at its offset, ending in the pool
    for race, cols in edges:
        row = offs[race][k.setdefault(race, 0)]
        k[race] += 1
        for lst, o in zip(cols, row):
            if bytes(pool[o:o + len(lst)]) != bytes(lst):
                raise ConversionError(f"marble: race {race} emitted edge list differs at pool offset {o}")
    print("// Terrain edge diagonals (0x264aa): column c of scroll frame k (the ultimate race has 4,",
          file=f)
    print("// others 1) of a race: ascending diagonals < maxd from marble_edge_lists()[edges()[36 *",
          file=f)
    print("// k + c]]; the first entry >= d is the edge, the last is maxd - 1.", file=f)
    _emit_table(run, f, "marble_edge_lists", "uint8_t", "", pool)
    edges_at = {race: _table_add(run, _words([o for row in rows for o in row]))
                for race, rows in sorted(offs.items())}
    size3["edge lists"] = len(pool) + 2 * sum(len(r) * 36 for r in offs.values()) + 6 * 8
    codes_at, records_at = {}, {}
    unpacked = 0
    for n, (index, codes, records, _, bases) in enumerate(races):
        if len(bases) != terrains[n].maxd or len(index) != terrains[n].maxd // 2 * _PAIR_BYTES:
            raise ConversionError(f"marble: race {n} bases/index do not match its {terrains[n].maxd} diagonals")
        raw = struct.pack(f"<{len(bases)}H", *bases) + index
        unpacked = max(unpacked, len(raw))
        emit_compressed(f, f"marble_terrain_{n}", "unsigned char", "", len(raw), list(raw), plan=False)
        size["terrain index + bases"] = (size.get("terrain index + bases", 0)
                                         + len(lzma_compress(raw)))
        key = tuple(codes)
        if key not in codes_at:
            codes_at[key] = _table_add(run, _words(codes))
            size["terrain codes"] = size.get("terrain codes", 0) + 2 * len(codes)
        records_at[n] = _table_add(run, bytes(records))
        size["terrain records"] = size.get("terrain records", 0) + len(records)
    print("// bytes of the largest unpacked race terrain (Arena buffer)", file=f)
    print(f"static const uint16_t MARBLE_TERRAIN_MAX = {unpacked};", file=f)
    print("static const MarbleTerrainInfo marble_terrain[6] = {", file=f)
    for n, (index, codes, records, _, bases) in enumerate(races):
        t = terrains[n]
        print(f"  {{ marble_terrain_{n}_blob, {len(index) // _PAIR_BYTES}, "
              f"{codes_at[tuple(codes)]}, {len(codes)}, {records_at[n]}, "
              f"{t.maxd}, {len(records)}, {ms.s16(run.sim.w(t.desc + _DESC_TOP))}, "
              f"{ms.s16(run.sim.w(t.desc + _DESC_RANGE))}, {run.sim.w(t.desc + _DESC_CELL)}, "
              f"{edges_at[n]}, {len(offs[n])} }},", file=f)
    print("};", file=f)
    size["terrain info"] = 6 * 32  # 32 bit pointers, padded

    race_sweeps(run, ms.Mem, terrains)
    print("// Program ROM tables (romconv/romconv/games/marble/progrom.py): marble_<name>[i], i",
          file=f)
    print("// checked against the table size; marble_<name>_of[race] the entries of a race's list;",
          file=f)
    print("// marble_<kind> spans for the data the work RAM addresses (marble_mem.h accessors).",
          file=f)
    tables = pr.emit(f, run.sim, pr.defs(run, terrains), partial(_table_add, run))
    for s, t in zip((size, size3, size4, size4b, size4b1, size4b2, size4b3, size4b4, size4b5,
                     size4b6), tables):
        s["prog tables"] = t

    tiles, pal = _mo_sprites(rom)
    first = [0xFF] * _MO_CODES.stop
    for k, code in enumerate(_MO_CODES):
        first[code] = k
    print("// Marble motion objects: tile k of an MO of code c and height h (k < h) is", file=f)
    print("// marble_mo_tiles[marble_mo_code_first[c] + k] (0xff: not emitted); pen 0", file=f)
    print("// transparent, colour marble_mo_palette[pen] (palette RAM 0x100 + set * 16).", file=f)
    print(f"static const uint8_t MARBLE_MO_CODES = 0x{_MO_CODES.stop:02x};", file=f)
    print("static const uint8_t marble_mo_code_first[MARBLE_MO_CODES] = {\n"
          + _c_bytes(first) + "\n};", file=f)
    print(f"static const uint8_t MARBLE_MO_TILES = {len(tiles)};", file=f)
    _emit_table(run, f, "marble_mo_tiles", "uint8_t", "[64]", bytes(p for t in tiles for p in t))
    print("static const uint16_t marble_mo_palette[16] = {\n" + _c_words(pal) + "\n};",
          file=f)
    size["mo tiles"] = 64 * len(tiles) + len(first)
    size["mo palette"] = 32

    for n, (index, codes, records, _, bases) in enumerate(races):
        run.ctx.log.info(f"terrain {n} {md.NAMES[n]}: {terrains[n].maxd} diagonals, index {len(index)} B, "
              f"{len(codes)} codes, records {len(records)} B, bases {2 * len(bases)} B")
    return size


def _emit_objects(run, f, rom, levels, size3):
    """Phase 3 data besides the program ROM tables: playfield patches of the level objects and the
    occlusion mask tiles. Sizes into size3."""
    patches = od.patches(run.sim)
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
            raise ConversionError(f"marble: patch {rec:05x} does not fit the byte fields")
    blocks = sorted(run.patch_words)
    for race, blk in blocks:
        print(f"static const uint16_t marble_patch_words_{blk:05x}[] = {{\n"
              f"{_c_words(run.patch_words[(race, blk)])}\n}};", file=f)
    print(f"static const MarblePatch marble_patches[{len(patches)}] = {{", file=f)
    for race, rec, col, row, w, h, blk in patches:
        print(f"  {{ 0x{rec:05x}, {race}, {col}, {row}, {w}, {h}, marble_patch_words_{blk:05x} }},",
              file=f)
    print(f"}};\nstatic const uint8_t MARBLE_PATCHES = {len(patches)};", file=f)
    size3["patches"] = 16 * len(patches) + sum(2 * len(w) for w in run.patch_words.values())
    # the closed state of a patched race shows the map as decoded: per race, one record
    # (the first frame of the gate) must equal the decoded map words
    for race in sorted({p[0] for p in patches}):
        mapw = levels[race][2]
        if not any(all(mapw[(row + r) * md.COLS + col + c] == run.patch_words[(rc, blk)][r * w + c]
                       for r in range(h) for c in range(w))
                   for rc, rec, col, row, w, h, blk in patches if rc == race):
            raise ConversionError(f"marble: race {race}: no patch frame equals the decoded level map, "
                  "patch words are wrong")

    codes = od.mask_tile_codes(run.sim)
    tiles = []
    for code in codes:
        tile, _ = md.mo_lookup(rom, code)
        px = md.mo_tile(rom, tile)
        if max(px) > 1:
            raise ConversionError(f"marble: mask tile {code:03x} uses pen {max(px)} (only pens 0/1 are masks)")
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
    _emit_table(run, f, "marble_mask_tiles", "uint8_t", "[8]", bytes(v for t in dense for v in t))
    print("static inline uint16_t marble_mask_index(uint16_t code) {\n"
          "  return (uint16_t)(code - MARBLE_MASK_FIRST) < MARBLE_MASK_CODES\n"
          "      ? (uint16_t)(code - MARBLE_MASK_FIRST) : 0xffff;\n}", file=f)
    size3["mask tiles"] = 8 * len(dense)
    run.ctx.log.info(f"patches: {len(patches)} records, {len(run.patch_words)} blocks; mask tiles: {len(tiles)} "
          f"of codes 0x{first:03x}-0x{last:03x}")


_SHADE_COLOURS = (0x343, 0x3f2, 0x3f3)   # palette entries the wave strip pens 2/3 select
_HDR_BASE = 0x20000                      # frame header address table: (address - base) >> 1
_LIFT_PAL_FIRST = 0x161                  # tag 3 palette variants: set 6 pens 1-4


def _sprite_set(rom, sp):
    """Tile pixels (4 bpp, 32 B per tile: left pixel in the high nibble) of a race's sprite
    set, in sorted code order. A frame column of tiles c .. c + h - 1 reads consecutive
    tiles, so identical tiles are not merged (LZMA folds them at no index cost)."""
    out = bytearray()
    for code in sp.tile_codes:
        tile, cs = md.mo_lookup(rom, code)
        px = md.mo_tile(rom, tile)
        out += bytes(px[k] << 4 | px[k + 1] for k in range(0, 64, 2))
    return bytes(out)


def _sprite_check_codes(rom, sp, sets=None):
    """A column code carries its palette set in bits 11-13 (ORed by 0x26f3e): the tile and the
    set the lookup returns must equal the plain tile code (checked for the sets in use)."""
    for code in sp.tile_codes:
        for st in sets or _SETS_USED:
            t, cs = md.mo_lookup(rom, code | st << 11)
            if t != md.mo_lookup(rom, code)[0] or cs != st:
                raise ConversionError(f"marble: MO code {code:03x} with set {st} looks up tile {t:03x}, set {cs}")


_SETS_USED = mo.SETS


def _lift_palette(run, rom):
    """The four tag 3 palette variants (0x20ae4 commands) as RGB565 for set 6 pens 1-4."""
    sim = run.sim
    out = []
    for c in range(4):
        d = sim.l(mo.PAL_CMDS + 4 * c)
        n = sim.prog[d]
        dest = [(sim.l(mo.PAL_DESTS + sim.prog[d + 1] + 4 * k) - 0xb00000) // 2
                for k in range(n)]
        if dest != [_LIFT_PAL_FIRST + k for k in range(4)]:
            raise ConversionError(f"marble: lift palette command {c} writes {dest}")
        out.append([rgb565_swapped(*md.irgb_hw(sim.w(d + 2 + 2 * k))) for k in range(4)])
    return out


def _frame_table(run, sp, base):
    """Frame table of sprite set sp (addr, frames, cols tables in marble_tables), column tiles
    + base (sprite tile buffer index). Returns the column pool and the MarbleSpriteSet
    initializer of the three tables' offsets."""
    pool, first, seen = [], [], {}
    for fid in range(len(sp.cols)):
        cols = tuple((x, y, hh, tile + base) for x, y, hh, tile in sp.objs_cols(fid))
        for x, y, hh, tile in cols:
            if not (-128 <= x < 128 and -128 <= y < 128):
                raise ConversionError(f"marble: sprite column offset {x},{y} does not fit int8")
        if cols not in seen:
            seen[cols] = len(pool); pool += cols
        first.append(seen[cols])
    for h in sp.hdrs:
        if h & 1 or not _HDR_BASE <= h < _HDR_BASE + 0x20000:
            raise ConversionError(f"marble: frame header {h:05x} odd or outside 0x20000-0x3ffff")
    addr = [(h - _HDR_BASE) >> 1 for h in sp.hdrs]
    # MarbleSpriteFrame { uint8_t ncols; uint16_t first; }, MarbleSpriteCol { int8_t x, y;
    # uint8_t h; uint16_t tile; }: C layout, padding byte before the uint16_t
    frames = b"".join(struct.pack("<BxH", len(c), first[fid]) for fid, c in enumerate(sp.cols))
    cols = b"".join(struct.pack("<bbBxH", *c) for c in pool)
    return pool, f"{_table_add(run, frames)}, {_table_add(run, cols)}, {_table_add(run, _words(addr))}"


def _sets_flash(rom, sprites):
    """Flash bytes of the distinct sprite sets of sprites (tiles LZMA + 16, frames, deduplicated
    columns, header ids), as emit_sprites counts them."""
    total, keys = 0, set()
    for sp in sprites:
        tiles = _sprite_set(rom, sp)
        key = (tuple(sp.hdrs), tuple(tuple(c) for c in sp.cols), tuple(sp.tile_codes), tiles)
        if key in keys:
            continue
        keys.add(key)
        pool = {tuple(sp.objs_cols(fid)) for fid in range(len(sp.cols))}
        total += len(lzma_compress(tiles)) + 16 + 4 * len(sp.cols) + 2 * len(sp.hdrs)
        total += 6 * sum(len(c) for c in pool)
    return total


def _emit_sprites(run, f, rom, size4, size4b1, size4b2):
    """Phase 4a: level object sprites per race (MARBLE.md "Level object sprites"): tile set,
    frame table, palettes. Races with the same frames and tiles share one set."""
    sets, race_set, keys = [], [], {}
    for n, sp in enumerate(run.sprites):
        _sprite_check_codes(rom, sp)
        tiles = _sprite_set(rom, sp)
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
    print("static_assert(sizeof(MarbleSpriteCol) == 6 && sizeof(MarbleSpriteFrame) == 4,\n"
          "              \"frame_table() packs this layout\");", file=f)
    print("struct MarbleSpriteSet {\n  uint16_t frames_at, cols_at, addr_at; uint8_t nhdr, nframes; "
          "uint16_t ntiles;\n"
          "  const MarbleSpriteFrame *frames() const { "
          "return marble_table<MarbleSpriteFrame>(frames_at); }\n"
          "  const MarbleSpriteCol *cols() const { return marble_table<MarbleSpriteCol>(cols_at); }\n"
          "  const uint16_t *addr() const { return marble_table<uint16_t>(addr_at); }\n};", file=f)
    sizes = {"tiles": 0, "frames": 0, "columns": 0, "header ids": 0}
    ndeath = len(run.death.tile_codes)
    tables = []
    for k, (n, sp, tiles) in enumerate(sets):
        pool, at = _frame_table(run, sp, ndeath)
        tables.append(at)
        emit_compressed(f, f"marble_spr{k}_tiles", "unsigned char", "[32]", len(sp.tile_codes),
                        list(tiles), plan=False)
        packed = len(lzma_compress(tiles))
        names = "/".join(md.NAMES[r] for r in range(md.LEVELS) if race_set[r] == k)
        run.ctx.log.info(f"sprite set {k} ({names}): {len(sp.hdrs)} frame headers + "
              f"{len(sp.cols) - len(sp.hdrs)} strip entries, {len(sp.tile_codes)} tiles, "
              f"{len(tiles)} B raw (Arena), {packed} B LZMA, {len(pool)} columns "
              f"(of {sum(len(c) for c in sp.cols)})")
        sizes["tiles"] += packed + 16
        sizes["frames"] += 4 * len(sp.cols)
        sizes["columns"] += 6 * len(pool)
        sizes["header ids"] += 2 * len(sp.hdrs)
    print("static const MarbleSpriteSet marble_sprites[%d] = {" % len(sets), file=f)
    for k, (n, sp, tiles) in enumerate(sets):
        print(f"  {{ {tables[k]}, "
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
        pals.append([[rgb565_swapped(*rgb[0x100 + st * 16 + pen]) for pen in range(8)]
                     for st in mo.SETS])
        for e in range(0x300, 0x400):
            if e not in _SHADE_COLOURS and rgb[e] != (0, 0, 0):
                raise ConversionError(f"marble: race {n} shade palette entry {e:03x} not black")
        shade.append([rgb565_swapped(*rgb[e]) for e in _SHADE_COLOURS])
    print("static const uint16_t marble_sprite_pal[6][4][8] = {", file=f)
    for pr in pals:
        print("  {" + ", ".join("{" + ",".join(f"0x{v:04x}" for v in row) + "}" for row in pr)
              + "},", file=f)
    print("};", file=f)
    print("static const uint16_t marble_sprite_shade[6][3] = {", file=f)
    for sh in shade:
        print("  {" + ",".join(f"0x{v:04x}" for v in sh) + "},", file=f)
    print("};", file=f)
    lift = _lift_palette(run, rom)
    print("static const uint16_t marble_lift_pal[4][4] = {", file=f)
    for row in lift:
        print("  {" + ",".join(f"0x{v:04x}" for v in row) + "},", file=f)
    print("};", file=f)
    sizes["palettes"] = 2 * (6 * 32 + 6 * 3 + 16)
    sizes["set table"] = 16 * len(sets) + 6
    size4.update(sizes)
    plain = [mo.Sprites(run.sim, n, steelies=False) for n in range(md.LEVELS)]
    size4b1["sprite sets"] = _sets_flash(rom, run.sprites) - _sets_flash(rom, plain)
    run.ctx.log.info("sprite frame ids per race: " + ", ".join(
        f"{md.NAMES[r]} {len(run.sprites[r].cols)}" for r in range(md.LEVELS)))


_DEATH_SETS = (0, 2)   # MO palette sets of the death frames: player 1 (+0x19 = 0), overlay 0x1000
_STEELIE_SET = 5       # a shattering steelie draws the shatter frames in set 5 (race palette)
_SLINKY_SET = 4        # slinky frames (death set), palette equal in the slinky races 1, 2, 5
_SLINKY_RACES = (1, 2, 5)
_ACID_SET = 6          # acid frames (death set), palette equal in the acid races 2, 5
_ACID_RACES = (2, 5)
_BIRD_SET = 7          # silly race birds (death set), palette set 7 of the silly race
_BIRD_RACES = (4,)
_MINI_SETS = (5, 6, 4)  # silly race minis of type 7, 8, 9 (death set rows 5, 6, 7)
_PISTON_SET = 4        # aerial race pistons (death set row 8), palette set 4 of the aerial race
_PISTON_RACES = (3,)


def _race_pal(rom, st, races, what):
    """Palette set st, pens 0-7 RGB565, equal in races (else fatal)."""
    pal = {tuple(rgb565_swapped(*md.palette(rom, n, True)[0x100 + st * 16 + pen])
                 for pen in range(8)) for n in races}
    if len(pal) != 1:
        raise ConversionError(f"marble: {what} palette differs between the {what} races")
    return pal.pop()


def _hud_data(rom):
    """Phase 3b (MARBLE.md "HUD"): glyph subset, code map, palettes as RGB565."""
    glyphs = hud.font(rom)
    codes = [c for c in hud.glyph_codes(rom) if any(any(line) for line in glyphs[c])]
    rows = [hud.glyph_rows(glyphs[c]) for c in codes]
    gmap = [hud.NO_GLYPH] * hud.CODES
    for k, c in enumerate(codes):
        gmap[c] = k
    rgb = lambda w: rgb565_swapped(*md.irgb_hw(w))
    pal = [rgb(md.u16(rom, md.PAL_ALPHA + 2 * i)) for i in range(32)]
    pulse = [rgb(md.u16(rom, hud.PULSE + 2 * i)) for i in range(16)]
    return codes, rows, gmap, pal, pulse, rgb


def _emit_hud(run, f, rom, size3b):
    codes, rows, gmap, pal, pulse, rgb = _hud_data(rom)
    print("\n// ---- Phase 3b: HUD (MARBLE.md \"HUD\", dev/marble/marble_hud.py)", file=f)
    for line in (
            "// Alpha font subset (136032.104.f5): 8 rows per glyph, 2 bpp, left pixel in bits",
            "// 15-14, pens 0-3 (0 transparent unless opaque). marble_hud_glyph maps an alpha code",
            "// (0-0x7f small font, 0x100-0x17f big font) to its glyph, MARBLE_HUD_NO_GLYPH: blank.",
            "// Palette: colour * 4 + pen, ROM 0x1FBD0; pulse table 0x20b34 (score fade, timer",
            "// flash); bonus colours of 0x118d2."):
        print(line, file=f)
    print(f"static const uint8_t MARBLE_HUD_NO_GLYPH = 0x{hud.NO_GLYPH:02x};", file=f)
    print(f"static const uint8_t MARBLE_HUD_GLYPHS = {len(rows)};", file=f)
    _emit_table(run, f, "marble_hud_glyphs", "uint16_t", "[8]", _words([v for g in rows for v in g]))
    print(f"static const uint16_t MARBLE_HUD_CODES = 0x{hud.CODES:x};", file=f)
    _emit_table(run, f, "marble_hud_glyph", "uint8_t", "", bytes(gmap))
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


def _particle_palettes(rom):
    """RGB565 pens 0-5 of the particle colours: title sets 0/1 (palette set 0), palette fill
    variants per set (0x26cfa)."""
    rgb = lambda w: rgb565_swapped(*md.irgb_hw(w))
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


def _emit_sound(run, f, size):
    """Phase 5 (MARBLE.md "Sound"): the C64 tune with the jingle song, the command table, the
    arcade's sound program for the effects (marble_soundboard)."""
    msid.selfcheck()
    tune = read_user_file(run.ctx, _SID_FILE, None, "download GAMES/M-R/Marble_Madness.sid from the HVSC "
                          "(hvsc.c64.org) into romszip/")
    sid, table, silent, alias, ram, songs = msid.build(tune)
    print("static const uint8_t MARBLE_SND_BOARD = 0, MARBLE_SND_SONG = 1, MARBLE_SND_STOP = 2;", file=f)
    print("struct MarbleSoundCmd { uint8_t kind; uint8_t arg; };", file=f)
    print("static const MarbleSoundCmd marble_sound_cmds[128] = {\n  "
          + ", ".join(f"{{ {k}, {a} }}" for k, a in table) + "\n};", file=f)
    print(f"static const uint8_t MARBLE_SID_SILENT = {silent};", file=f)
    print(f"static const uint16_t MARBLE_SID_ALIAS = 0x{alias:04x};", file=f)
    print(f"static const uint16_t MARBLE_SID_RAM = 0x{ram:04x};", file=f)
    print(f"static const uint8_t MARBLE_SID_SONGS = {struct.unpack('>H', sid[0x0e:0x10])[0]};", file=f)
    print(f"static const uint16_t MARBLE_SID_SIZE = {len(sid)};", file=f)
    emit_compressed(f, "marble_sid", "unsigned char", "", len(sid), list(sid), plan=False)
    emit_plain(f, "marble_snd_rom", "unsigned char", "", len(run.rom.sound), "  " + hex_block(run.rom.sound),
               plan=False)
    size["sound commands"] = 256
    size["sid"] = len(lzma_compress(sid))
    size["sound program"] = len(run.rom.sound)
    (run.ctx.tmp / "marble.sid").write_bytes(sid)
    with run.fixture(_SOUND_FIXTURE) as g:
        print(f"// Generated by {module_path(__file__)} (CRCs only).", file=g)
        print(f"static const uint32_t MARBLE_SID_CRC = 0x{zlib.crc32(sid):08x};", file=g)
        print(f"static const uint32_t MARBLE_SND_ROM_CRC = 0x{zlib.crc32(run.rom.sound):08x};", file=g)
        print(f"static const uint8_t MARBLE_SONG_3F = {songs[0x3f]};", file=g)


def _emit_attract(f, rom, size6a):
    print("\n// ---- Phase 6a: title (MARBLE.md \"Attract\", dev/marble/marble_attract.py)", file=f)
    print("// High score table defaults (ROM 0x1eea0), table order", file=f)
    print("struct MarbleHiscore { char initials[4]; uint32_t score; };", file=f)
    print("static const MarbleHiscore marble_hiscore_defaults[10] = {", file=f)
    for ini, sc in att.defaults(rom):
        print(f"  {{ \"{ini}\", {sc} }},", file=f)
    print("};", file=f)
    title, fill = _particle_palettes(rom)
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


def _emit_demo(f, rom, size6b):
    print("\n// ---- Phase 6b: attract demo (MARBLE.md \"Demo race\", dev/marble/marble_demo.py)", file=f)
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


def _write_attract_fixture(run, rom, title):
    crc = lambda fmt, v: zlib.crc32(struct.pack(fmt, *v))
    rows, tiles, mapw, pal = title
    tpal, fill = _particle_palettes(rom)
    hs = b"".join(ini.encode() + b"\0" + struct.pack("<I", sc) for ini, sc in att.defaults(rom))
    part = b"".join(md.mo_tile(rom, md.mo_lookup(rom, c)[0]) for c in (1, 2, 3, 4))
    with run.fixture(_ATTRACT_FIXTURE) as f:
        print(f"// Generated by {module_path(__file__)}; do not edit.", file=f)
        print("// Counts and CRC32s only, no ROM data (dev/marble/marble_attract.py).", file=f)
        print(f"static const uint16_t MARBLE_ATT_FIX_TITLE_ROWS = {rows};", file=f)
        print(f"static const uint16_t MARBLE_ATT_FIX_TITLE_TILES = {len(tiles)};", file=f)
        print(f"static const uint32_t MARBLE_ATT_FIX_MAP_CRC = 0x{crc(f'<{len(mapw)}H', mapw):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_ATT_FIX_TILES_CRC = 0x{zlib.crc32(bytes(p for t in tiles for p in t)):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_ATT_FIX_PAL_CRC = 0x{crc('<32H', pal[:32]):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_ATT_FIX_HISCORE_CRC = 0x{zlib.crc32(hs):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_ATT_FIX_TITLE_PART_CRC = 0x{crc('<12H', [v for p in tpal for v in p]):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_ATT_FIX_FILL_CRC = 0x{crc('<96H', [v for vs in fill for p in vs for v in p]):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_ATT_FIX_PART_TILES_CRC = 0x{zlib.crc32(part):08x}u;", file=f)


def _write_hud_fixture(run, rom):
    codes, rows, gmap, pal, pulse, _ = _hud_data(rom)
    crc = lambda fmt, v: zlib.crc32(struct.pack(fmt, *v))
    s = hud.font(rom)[ord("S")]
    with run.fixture(_HUD_FIXTURE) as f:
        print(f"// Generated by {module_path(__file__)}; do not edit.", file=f)
        print("// Counts and CRC32s only, no ROM data (dev/marble/marble_hud.py).", file=f)
        print(f"static const uint32_t MARBLE_HUD_FIX_GLYPHS = {len(rows)};", file=f)
        print(f"static const uint32_t MARBLE_HUD_FIX_GLYPH_CRC = 0x{crc(f'<{8 * len(rows)}H', [v for g in rows for v in g]):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_HUD_FIX_MAP_CRC = 0x{crc(f'{len(gmap)}B', gmap):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_HUD_FIX_PAL_CRC = 0x{crc('<32H', pal):08x}u;", file=f)
        print(f"static const uint32_t MARBLE_HUD_FIX_PULSE_CRC = 0x{crc('<16H', pulse):08x}u;", file=f)
        rgb = lambda w: rgb565_swapped(*md.irgb_hw(w))
        print("// remake layout cases (dev/marble/marble_hud.py LAYOUT_CASES): HUD state, CRC32 of the",
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


def _emit_death(run, f, rom, size4b, size4b2, size4b3, size4b4, size4b5, size4b6):
    """Phase 4b-0: player death sprites of every race (MARBLE.md "Phase 4b-0 data"): tile set
    (sparkle stars included), frame table, palettes of sets 0 and 2. Phase 4b-2: the slinky
    frames and their palette (set 4) join it, Phase 4b-3 the acid frames (set 6), Phase 4b-4a
    the silly race bird frames (set 7), Phase 4b-4b the minis, Phase 4b-5 the aerial
    pistons (aerial set 4)."""
    ds = run.death
    _sprite_check_codes(rom, ds, _DEATH_SETS + (_STEELIE_SET, _SLINKY_SET, _ACID_SET, _BIRD_SET)
                       + _MINI_SETS + (_PISTON_SET,))
    first = ds.tile_of[mo.STAR_CODE]
    if any(ds.tile_of[mo.STAR_CODE + k] != first + k for k in range(mo.STARS)):
        raise ConversionError("marble: sparkle star tiles not consecutive")
    tiles = _sprite_set(rom, ds)
    print("\n// ---- Phase 4b-0: player death sprites (MARBLE.md \"Phase 4b-0 data\")", file=f)
    for line in (
            "// Death set, every race: frames of the player animation lists (dizzy, eaten, shatter,",
            "// dissolve, rl(rl(+0x5a))) and the shatter overlay (rl(rl(+0x62))), ids as the race",
            "// sets (header frames only, no strip). Its MARBLE_DEATH_TILES tiles open the sprite",
            "// tile buffer; columns index them directly. Sparkle star MO code MARBLE_STAR_CODE + k",
            "// (k < MARBLE_STARS, height 1, 0x13ade) is buffer tile MARBLE_STAR_TILE + k."):
        print(line, file=f)
    pool, at = _frame_table(run, ds, 0)
    emit_compressed(f, "marble_death_tiles", "unsigned char", "[32]", len(ds.tile_codes),
                    list(tiles), plan=False)
    print(f"static const uint16_t MARBLE_DEATH_TILES = {len(ds.tile_codes)};", file=f)
    enemy = {h for a in mo.SLINKY_LISTS + mo.ACID_LISTS + mo.BIRD_LISTS + mo.MINI_LISTS
             + mo.PISTON_LISTS for h in mo.anim_list(run.sim, a)}
    early = {h for a in mo.PISTON_LISTS for h in mo.anim_list(run.sim, a)}
    nfirst = len(early)
    nplayer = len(ds.hdrs) - len(enemy)
    if any(ds.ids[h] >= nfirst for h in early) or \
            any(nfirst <= ds.ids[h] < nfirst + nplayer for h in enemy):
        raise ConversionError("marble: death set not pistons, player frames, other enemies")
    print("// Frame ids MARBLE_DEATH_FIRST .. + MARBLE_DEATH_FRAMES are player death frames; the",
          file=f)
    print("// piston frames come before them, the other enemy frames (acid, slinkies, birds,", file=f)
    print("// minis) after them (ids by header address).", file=f)
    print(f"static const uint8_t MARBLE_DEATH_FIRST = {nfirst};", file=f)
    print(f"static const uint8_t MARBLE_DEATH_FRAMES = {nplayer};", file=f)
    print(f"static const uint16_t MARBLE_STAR_CODE = 0x{mo.STAR_CODE:03x};", file=f)
    print(f"static const uint8_t MARBLE_STARS = {mo.STARS};", file=f)
    print(f"static const uint16_t MARBLE_STAR_TILE = {first};", file=f)
    print(f"static const MarbleSpriteSet marble_death_set = {{ {at}, {len(ds.hdrs)}, {len(ds.cols)}, "
          f"{len(ds.tile_codes)} }};", file=f)
    pals = {tuple(tuple(rgb565_swapped(*md.palette(rom, n, True)[0x100 + st * 16 + pen])
                        for pen in range(8)) for st in _DEATH_SETS) for n in range(md.LEVELS)}
    if len(pals) != 1:
        raise ConversionError("marble: death frame palettes differ between races")
    slinky = _race_pal(rom, _SLINKY_SET, _SLINKY_RACES, "slinky")
    acid = _race_pal(rom, _ACID_SET, _ACID_RACES, "acid")
    bird = _race_pal(rom, _BIRD_SET, _BIRD_RACES, "bird")
    minis = tuple(_race_pal(rom, st, _BIRD_RACES, "mini") for st in _MINI_SETS)
    piston = _race_pal(rom, _PISTON_SET, _PISTON_RACES, "piston")
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
    run.ctx.log.info(f"death set: {len(ds.hdrs)} frame headers, {len(ds.tile_codes)} tiles (stars at "
          f"{first}), {len(tiles)} B raw (Arena), {packed} B LZMA, {len(pool)} columns "
          f"(of {sum(len(c) for c in ds.cols)})")
    plain = mo.DeathSprites(run.sim, slinkies=False, acid=False, birds=False, minis=False, pistons=False)
    packed0 = len(lzma_compress(_sprite_set(rom, plain)))
    pool0 = {tuple(plain.objs_cols(fid)) for fid in range(len(plain.cols))}
    cols0 = 6 * sum(len(c) for c in pool0)
    sls = mo.DeathSprites(run.sim, acid=False, birds=False, minis=False, pistons=False)
    bds = mo.DeathSprites(run.sim, minis=False, pistons=False)
    mns = mo.DeathSprites(run.sim, pistons=False)
    packed4 = len(lzma_compress(_sprite_set(rom, mns)))
    cols4 = 6 * sum(len(c) for c in {tuple(mns.objs_cols(fid)) for fid in range(len(mns.cols))})
    packed3 = len(lzma_compress(_sprite_set(rom, bds)))
    cols3 = 6 * sum(len(c) for c in {tuple(bds.objs_cols(fid)) for fid in range(len(bds.cols))})
    packed1 = len(lzma_compress(_sprite_set(rom, sls)))
    cols1 = 6 * sum(len(c) for c in {tuple(sls.objs_cols(fid)) for fid in range(len(sls.cols))})
    acs = mo.DeathSprites(run.sim, birds=False, minis=False, pistons=False)
    packed2 = len(lzma_compress(_sprite_set(rom, acs)))
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


def convert(ctx):
    rom = md.load(lambda name, sha1, zip: ctx.rom(name, sha1, zip=zip))
    run = Run(ctx, rom, ms.make_rom(rom))
    levels = [_convert_level(run, n) for n in range(md.LEVELS)]
    title = _convert_title(run)
    if max(len(l[1]) for l in levels) > _MAX_TILES:
        raise ConversionError(f"marble: a tile set exceeds MAX_TILES {_MAX_TILES}")
    if max(l[0] for l in levels) != _MAX_ROWS or max(len(l[1]) for l in levels) != _MAX_TILES:
        raise ConversionError("MAX_ROWS / MAX_TILES changed, update marble_level.h and this file")
    with ctx.header("marble_rom.h") as f:
        print(f"// Generated by {module_path(__file__)}; do not edit.", file=f)
        print("#ifndef MARBLE_ROM_H\n#define MARBLE_ROM_H\n#include <stdint.h>\n"
              "#include \"marble_mem.h\"", file=f)
        print("// Tables read through name() or a struct accessor live in marble_tables: one LZMA",
              file=f)
        print("// blob that marble_physics::loadRom() unpacks to the Arena, at 2-aligned offsets.",
              file=f)
        print("extern const uint8_t *marble_tables;", file=f)
        print("template <typename T> static inline const T *marble_table(uint16_t at) {\n"
              "  return (const T *)(marble_tables + at);\n}", file=f)
        print("// Program ROM tables (progrom.py): marble_<name>[i], i < N or marble_physics::fail",
              file=f)
        print("template <typename T, uint16_t N> struct MarbleTable {\n  uint16_t at;\n"
              "  const char *what;\n  const T &operator[](uint16_t i) const {\n"
              "    if (i >= N) { marble_physics::fail(what, i); }\n"
              "    return marble_table<T>(at)[i];\n  }\n};", file=f)
        print("struct MarbleRange { uint16_t first, n; };  // entries of one race's list", file=f)
        print("// Program ROM bytes by 68010 address, sorted spans of one kind (marble_mem.h accessors)",
              file=f)
        print("struct MarbleSpan { uint32_t addr; uint16_t len, off; };", file=f)
        print("struct MarbleSpans {\n  const MarbleSpan *spans;\n  uint8_t n;\n  const char *what;\n"
              "  const uint8_t *at(uint32_t a, uint8_t len) const;  // len bytes at a, or fail\n};",
              file=f)
        print("struct MarbleLevelInfo { uint16_t rows; uint16_t tiles; };", file=f)
        print(f"static const uint8_t MARBLE_TITLE_LEVEL = {md.LEVELS};  // the title playfield",
              file=f)
        print("static const MarbleLevelInfo marble_levels[7] = {", file=f)
        print("  " + ", ".join(f"{{ {r}, {len(t)} }}" for r, t, _, _ in levels + [title])
              + "\n};", file=f)
        for n, (rows, tiles, mapw, _) in enumerate(levels + [title]):
            emit_compressed(f, f"marble_l{n}_map", "unsigned short", "", len(mapw), mapw, plan=False)
            emit_compressed(f, f"marble_l{n}_tiles", "unsigned char", "[64]", len(tiles),
                            [p for t in tiles for p in t], plan=False)
        print("static const FlashAsset<unsigned short, COMPRESSED> marble_maps[7] = { "
              + ", ".join(f"marble_l{n}_map_blob" for n in range(md.LEVELS + 1)) + " };", file=f)
        print("static const FlashAsset<unsigned char[64], COMPRESSED> marble_tiles[7] = { "
              + ", ".join(f"marble_l{n}_tiles_blob" for n in range(md.LEVELS + 1)) + " };", file=f)
        _emit_table(run, f, "marble_pf_palette", "uint16_t", "[128]",
                   _words([v for _, _, _, pal in levels + [title] for v in pal]))
        size3, size4, size4b, size4b1, size4b2, size4b3, size4b4, size4b5, size4b6 = \
            {}, {}, {}, {}, {}, {}, {}, {}, {}
        run.sprites = [mo.Sprites(run.sim, n) for n in range(md.LEVELS)]
        run.death = mo.DeathSprites(run.sim)
        size = _emit_physics(run, f, rom, size3, size4, size4b, size4b1, size4b2, size4b3, size4b4,
                            size4b5, size4b6)
        _emit_objects(run, f, rom, levels, size3)
        _emit_sprites(run, f, rom, size4, size4b1, size4b2)
        _emit_death(run, f, rom, size4b, size4b2, size4b3, size4b4, size4b5, size4b6)
        size3b = {}
        _emit_hud(run, f, rom, size3b)
        size6a = {"title map + tiles": len(lzma_compress(struct.pack(f"<{len(title[2])}H", *title[2])))
                  + len(lzma_compress(bytes(p for t in title[1] for p in t))), "title palette": 256}
        _emit_attract(f, rom, size6a)
        size6b = {}
        _emit_demo(f, rom, size6b)
        size5 = {}
        _emit_sound(run, f, size5)
        emit_compressed(f, "marble_tables", "unsigned char", "", len(run.tables), list(run.tables))
        print("#endif", file=f)
    run.ctx.log.info(f"tables: {len(run.tables)} B raw (Arena), {len(lzma_compress(bytes(run.tables)))} B LZMA")
    _write_hud_fixture(run, rom)
    _write_attract_fixture(run, rom, title)
    run.ctx.log.info("phase 6a (title) flash: " + ", ".join(f"{k} {v}" for k, v in size6a.items())
          + f", total {sum(size6a.values())} B")
    run.ctx.log.info("phase 6b (demo) flash: " + ", ".join(f"{k} {v}" for k, v in size6b.items())
          + f", total {sum(size6b.values())} B")
    total5 = sum(size5.values())
    run.ctx.log.info("phase 5 (sound) flash: " + ", ".join(f"{k} {v}" for k, v in size5.items())
          + f", total {total5} B")
    if total5 > _SOUND_BUDGET:
        raise ConversionError(f"marble: phase 5 data {total5} B over the {_SOUND_BUDGET} B budget")
    total = sum(size.values())
    run.ctx.log.info("phase 2 flash: " + ", ".join(f"{k} {v}" for k, v in size.items()) + f", total {total} B")
    if total > _FLASH_BUDGET:
        raise ConversionError(f"marble: phase 2 data {total} B over the {_FLASH_BUDGET} B budget")
    total3 = sum(size3.values())
    run.ctx.log.info("phase 3 flash: " + ", ".join(f"{k} {v}" for k, v in size3.items())
          + f", total {total3} B")
    if total3 > _PHASE3_BUDGET:
        raise ConversionError(f"marble: phase 3 data {total3} B over the {_PHASE3_BUDGET} B budget")
    total4 = sum(size4.values())
    run.ctx.log.info("phase 4a flash: " + ", ".join(f"{k} {v}" for k, v in size4.items())
          + f", total {total4} B")
    run.ctx.log.info("phase 3b (HUD) flash: " + ", ".join(f"{k} {v}" for k, v in size3b.items())
          + f", total {sum(size3b.values())} B")
    total4b = sum(size4b.values())
    run.ctx.log.info("phase 4b-0 flash: " + ", ".join(f"{k} {v}" for k, v in size4b.items())
          + f", total {total4b} B")
    total4b1 = sum(size4b1.values())
    run.ctx.log.info("phase 4b-1 flash: " + ", ".join(f"{k} {v}" for k, v in size4b1.items())
          + f", total {total4b1} B")
    total4b2 = sum(size4b2.values())
    run.ctx.log.info("phase 4b-2 flash: " + ", ".join(f"{k} {v}" for k, v in size4b2.items())
          + f", total {total4b2} B")
    total4b3 = sum(size4b3.values())
    run.ctx.log.info("phase 4b-3 flash: " + ", ".join(f"{k} {v}" for k, v in size4b3.items())
          + f", total {total4b3} B")
    total4b4 = sum(size4b4.values())
    run.ctx.log.info("phase 4b-4a (birds, silly tubes) flash: " + ", ".join(f"{k} {v}" for k, v in size4b4.items())
          + f", total {total4b4} B")
    total4b5 = sum(size4b5.values())
    run.ctx.log.info("phase 4b-4b (minis) flash: " + ", ".join(f"{k} {v}" for k, v in size4b5.items())
          + f", total {total4b5} B")
    total4b6 = sum(size4b6.values())
    run.ctx.log.info("phase 4b-5 (pistons) flash: " + ", ".join(f"{k} {v}" for k, v in size4b6.items())
          + f", total {total4b6} B")
    with run.fixture(_LEVEL_FIXTURE) as f:
        print(f"// Generated by {module_path(__file__)}; do not edit.", file=f)
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
              + ", ".join(str(v) for v in run.sweep_max) + " };", file=f)
        print("static const uint32_t MARBLE_FIX_FRAME_CRC[6][3] = {", file=f)
        for rows, tiles, mapw, pal in levels:
            c = [_frame_crc(rows, tiles, mapw, pal, sx, 0) for sx in (0, _PAN // 2, _PAN)]
            print("  { " + ", ".join(f"0x{v:08x}u" for v in c) + " },", file=f)
        print("};", file=f)
    for n, (rows, tiles, _, _) in enumerate(levels):
        run.ctx.log.info(f"level {n} {md.NAMES[n]}: {rows} rows, {len(tiles)} tiles")
    for path, text in run.outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")
