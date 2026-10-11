#!/usr/bin/env python3
"""Marble Madness: per-level playfield tiles, maps and palettes for the galagino remake.

Each level gets its own tile set (only the tiles its map uses, in first-use order) so one
level fits fixed Arena buffers (MAX_TILES x 64 + MAX_ROWS x 36 x 2 bytes). Map word:
bits 0-9 local tile, 10-11 colour set, 15 hflip. Also writes the host test fixture.

Phase 2 (marble physics, MARBLE.md "Remake data"): terrain per race, the program ROM
blocks the physics reads, marble motion object tiles and palette.
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

ROOT = os.path.join(md.HERE, "..", "..", "..")
OUT = os.path.join(ROOT, "source", "src", "machines", "marble", "marble_rom.h")
FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_level", "marble_fixture.h")
TRACE_FIXTURE = os.path.join(ROOT, "source", "test", "test_marble_physics", "marble_trace.h")

MAX_ROWS = 145   # must match marble_level.h
MAX_TILES = 896  # must match marble_level.h
VIEW_W, VIEW_H, PAN = 240, 288, 48


def convert_level(rom, n):
    words = md.decode_level(rom, n)
    local = {}
    tiles = []
    mapw = []
    for row in words:
        for w in row:
            code, cs, flip = md.pf_lookup(rom, w)
            if code not in local:
                local[code] = len(tiles)
                tiles.append(md.pf_tile(rom, code))
                if len(tiles) > 1024:
                    fatal("marble: more than 1024 tiles in a level (map word has 10 tile bits)")
            if cs > 3:
                fatal(f"marble: colour set {cs} > 3 (map word has 2 colour bits)")
            mapw.append(local[code] | cs << 10 | (0x8000 if flip else 0))
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
    return bytes(index), t.codes, records, recs, t.bases


class RecMem(ms.Mem):
    """Mem that records the ROM addresses the physics reads."""
    prog = set()
    slap = {}  # race -> 68010 addresses in the slapstic window
    def rb(self, a):
        a &= 0xFFFFFF
        if a < 0x30000:
            RecMem.prog.add(a)
        elif 0x80000 <= a < 0x88000:
            RecMem.slap.setdefault(self.t.race, set()).add(a)
        return super().rb(a)


class BlockMem(ms.Mem):
    """Mem that serves ROM only from the emitted blocks and records: a read outside fails."""
    blocks = []
    records = {}
    def rb(self, a):
        a &= 0xFFFFFF
        if 0x400000 <= a < 0x402000:
            return super().rb(a)
        if 0x80000 <= a < 0x88000:
            base, data = BlockMem.records[self.t.race]
            if base <= a < base + len(data):
                return data[a - base]
        for addr, data in BlockMem.blocks:
            if addr <= a < addr + len(data):
                return data[a - addr]
        fatal(f"marble: ROM read {a:06x} outside the emitted blocks")


def physics_runs(mem):
    """Race starts of all races and every fixture tick, on Mem class mem. Returns positions."""
    out = []
    terrains = [ms.Terrain(n) for n in range(md.LEVELS)]
    for n in range(md.LEVELS):
        m = mem(terrains[n], bytes(0x2000))
        ms.start_259b4(m, n)
        out.append(tuple(m.sl(ms.P + o) for o in (0xC, 0x10, 0x14)))
    for seg in ms.load_fixture(TRACE_FIXTURE):
        m = ms.fixture_mem(seg, terrains[seg["race"]], mem)
        if ms.replay_fixture(seg, m) is not None:
            fatal("marble: fixture replay differs, run marble_sim.py fixture")
        out.append(tuple(m.sl(ms.P + o) for o in (0xC, 0x10, 0x14)))
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


def prog_blocks(descs):
    """Program ROM ranges the physics reads: recorded reads plus whole tables."""
    RecMem.prog.clear(); RecMem.slap.clear()
    ref = physics_runs(RecMem)
    want = set(RecMem.prog)
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
    outside = sorted(want - covered)
    if outside:
        fatal("marble: physics reads ROM outside the known tables: "
              + ", ".join(f"{a:06x}" for a in outside[:16]))
    blocks = [(a, bytes(ms.PROG[a:e])) for a, e in merge(want | covered)]
    return blocks, ref


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


def emit_physics(f, rom):
    """Phase 2 data into marble_rom.h. Returns {item: flash bytes}."""
    size = {}
    terrains = [ms.Terrain(n) for n in range(md.LEVELS)]
    races = [terrain_race(t) for t in terrains]
    print("\n// ---- Phase 2: marble physics (MARBLE.md \"Remake data\")", file=f)
    print("// Terrain per race: 10 bit vertex index (54 bytes per diagonal pair, fields MSB first,",
          file=f)
    print("// field = column, +22 on odd diagonals, field 43 not stored = 0) into codes[];", file=f)
    print("// record codes 0x0001-0x07ff read records[code + quadrant]; bases[] per diagonal.",
          file=f)
    print("struct MarbleTerrainInfo {\n  const uint8_t *index; uint16_t pairs;\n"
          "  const uint16_t *codes; uint16_t ncodes;\n  const uint8_t *records;\n"
          "  const uint16_t *bases; uint16_t maxd;\n  uint16_t nrecords; uint32_t desc;\n};",
          file=f)
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
              f"marble_terrain_bases_{n}, {t.maxd}, {len(records)}, 0x{t.desc:05x} }},", file=f)
    print("};", file=f)
    size["terrain info"] = 6 * 32  # 32 bit pointers, padded

    BlockMem.records = {n: (r[3], r[2]) for n, r in enumerate(races)}
    blocks, ref = prog_blocks([t.desc for t in terrains])
    for n in range(md.LEVELS):
        recs, data = BlockMem.records[n]
        if any(not recs <= a < recs + len(data) for a in RecMem.slap.get(n, ())):
            fatal(f"marble: race {n} reads slapstic data outside its terrain records")
    BlockMem.blocks = blocks
    if physics_runs(BlockMem) != ref:
        fatal("marble: physics on the emitted blocks differs from the full ROM")
    print("// Program ROM bytes the physics reads (68010 address, sorted). Records and terrain",
          file=f)
    print("// come from marble_terrain; any other ROM address is a porting error.", file=f)
    print("struct MarbleProgBlock { uint32_t addr; uint16_t len; const uint8_t *data; };", file=f)
    for k, (a, data) in enumerate(blocks):
        print(f"static const uint8_t marble_prog_{a:05x}[] = {{\n{c_bytes(data)}\n}};", file=f)
    print(f"static const MarbleProgBlock marble_prog_blocks[{len(blocks)}] = {{", file=f)
    for a, data in blocks:
        print(f"  {{ 0x{a:05x}, {len(data)}, marble_prog_{a:05x} }},", file=f)
    print(f"}};\nstatic const uint8_t MARBLE_PROG_BLOCKS = {len(blocks)};", file=f)
    size["prog blocks"] = sum(len(d) for _, d in blocks) + 8 * len(blocks)

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
    print(f"static const unsigned char marble_mo_tiles[{len(tiles)}][64] = {{", file=f)
    for t in tiles:
        print("  {" + ",".join(str(p) for p in t) + "},", file=f)
    print("};", file=f)
    print("static const unsigned short marble_mo_palette[16] = {\n" + c_words(pal) + "\n};",
          file=f)
    size["mo tiles"] = 64 * len(tiles) + len(first)
    size["mo palette"] = 32

    for n, (index, codes, records, _, bases) in enumerate(races):
        print(f"terrain {n} {md.NAMES[n]}: {terrains[n].maxd} diagonals, index {len(index)} B, "
              f"{len(codes)} codes, records {len(records)} B, bases {2 * len(bases)} B")
    print(f"prog blocks: {len(blocks)}, " + ", ".join(f"{a:05x}+{len(d)}" for a, d in blocks))
    return size


def main():
    rom = md.load()
    levels = [convert_level(rom, n) for n in range(md.LEVELS)]
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
        size = emit_physics(f, rom)
        print("#endif", file=f)
    total = sum(size.values())
    print("phase 2 flash: " + ", ".join(f"{k} {v}" for k, v in size.items()) + f", total {total} B")
    if total > FLASH_BUDGET:
        fatal(f"marble: phase 2 data {total} B over the {FLASH_BUDGET} B budget")
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
