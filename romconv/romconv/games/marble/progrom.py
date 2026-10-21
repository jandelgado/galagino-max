"""Program ROM tables of the Marble Madness remake (MARBLE.md "Remake data").

The remake reads 68010 program ROM data by address (`Mem::romb`, blocks of marble_prog). This
module defines which bytes: whole tables, walked to their terminators, one set per phase.
phase_bytes() is the single source for the emitted blocks and for the coverage proof
(tests/test_games_marble.py replays every MAME fixture and race sweep on the model and fails
on a read outside)."""
from romconv.games.marble import data as md
from romconv.games.marble.sim import marble_acid as ac
from romconv.games.marble.sim import marble_bird as bd
from romconv.games.marble.sim import marble_enemy as me
from romconv.games.marble.sim import marble_mini as mn
from romconv.games.marble.sim import marble_mo as mo
from romconv.games.marble.sim import marble_piston as pp
from romconv.games.marble.sim import marble_sim as ms
from romconv.games.marble.sim import marble_slinky as sl
from romconv.games.marble.sim import objects_data as od


# Program ROM tables the physics, start, respawn and camera read (address, length). Read
# extents follow from the index ranges in marble_sim.py; the recorded reads below must fall
# inside these plus the descriptors, zone line and checkpoint lists.
_PROG_TABLES = (
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
_DESC_BYTES = 0x2e  # descriptor fields +0x00..+0x2d

# Phase 4b-0 tables the death code indexes (address, bytes): the fixture reads only parts
_DEATH_TABLES = (
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


def _steelie_bytes(run):
    """Phase 4b-1 tables (whole): spawn table pointers and entries per race (0xff end), every
    node list of an entry up to its 0xff, 0xff end, the steelie and object pointer tables, the
    pair list, the push table 0x24ad6 (|v high word| < 8), the score tables of k = 3."""
    sim = run.sim
    out = set(range(me.SPAWN_TABLE, me.SPAWN_TABLE + 4 * md.LEVELS))
    out |= set(range(me.OBJ_PTRS, me.OBJ_PTRS + 16)) | set(range(0x1effe, 0x1effe + 8))
    out |= set(range(me.PAIR_LIST, me.PAIR_LIST + 16)) | set(range(me.REPEL, me.REPEL + 8))
    out |= set(range(ms.SCORES + 6, ms.SCORES + 8)) | set(range(ms.SCORE_TEXTS + 12, ms.SCORE_TEXTS + 16))
    for race in range(md.LEVELS):
        e = sim.l(me.SPAWN_TABLE + 4 * race)
        while sim.prog[e] != 0xff:
            p = sim.l(e + 2)
            while not (sim.prog[p] == 0xff and sim.prog[p + 1] == 0xff):
                p += 6
            out |= set(range(sim.l(e + 2), p + 2)) | set(range(e, e + 8))
            e += 8
        out.add(e)
    return out


def _piston_bytes(run):
    """Phase 4b-5 tables (whole): first and reload timers, the 4 variant pointers of both
    kinds and their 12 word tables, cells, tops, the animation list (0xffffffff end)."""
    sim = run.sim
    out = set(range(pp.TIMER0, pp.TIMER0 + 2 * pp.FIXED)) | set(range(pp.TIMER, pp.TIMER + 2 * pp.FIXED))
    for base in (pp.VARIANT0, pp.VARIANT):
        out |= set(range(base, base + 16))
        for v in range(4):
            a = sim.l(base + 4 * v)
            out |= set(range(a, a + 2 * (len(pp.PISTONS) - pp.FIXED)))
    out |= set(range(pp.CELL_X, pp.CELL_X + 36)) | set(range(pp.CELL_Y, pp.CELL_Y + 36))
    out |= set(range(pp.TOP, pp.TOP + 72))
    for a in mo.PISTON_LISTS:
        out |= set(range(a, a + 4 * len(mo.anim_list(sim, a)) + 4))
    return out


def _frame_bytes(sim, lists, first, end):
    """Header bytes first..end - 1 of every frame of the animation lists."""
    out = set()
    for a in lists:
        for h in mo.anim_list(sim, a):
            out |= set(range(h + first, h + end))
    return out


_SHAPES = 7           # 0x1cd00 shape pointers at ms.SHAPES
_SHAPE_ENTRY = 16     # plane entry: normal (3 words), box (6 bytes), centre (3 bytes)
_SHAPE_LAST = 0x800   # normal x above: last entry of the shape


def _shape_bytes(run):
    """Tube wall shapes 0x1cd00 (whole): the shape pointers and the plane entries of every
    shape up to its last (normal x > 0x800)."""
    sim = run.sim
    out = set(range(ms.SHAPES, ms.SHAPES + 4 * _SHAPES))
    for k in range(_SHAPES):
        e = sim.l(ms.SHAPES + 4 * k)
        while True:
            out |= set(range(e, e + _SHAPE_ENTRY))
            if ms.s16(sim.w(e)) > _SHAPE_LAST:
                break
            e += _SHAPE_ENTRY
    return out


def _mini_bytes(run):
    """Phase 4b-4b tables (whole): the mini pointer table, the step tables 0x244b6/0x244d6,
    the cell bitmap 0x24496, the animation lists (0xffffffff end) and the draw list box
    bytes +4..+7 of their frames (0x1b12a types 7-9)."""
    sim = run.sim
    out = set(range(0x1f096, 0x1f096 + 36)) | set(range(mn.GRID, mn.GRID + 32))
    out |= set(range(mn.DX, mn.DX + 32)) | set(range(mn.DY, mn.DY + 32))
    for a in mo.MINI_LISTS:
        out |= set(range(a, a + 4 * len(mo.anim_list(sim, a)) + 4))
    return out | _frame_bytes(sim, mo.MINI_LISTS, 4, 8)


def _bird_bytes(run):
    """Phase 4b-4a tables (whole): the bird pointer table, the spawn cells, the animation
    lists (0xffffffff end)."""
    sim = run.sim
    out = set(range(0x1f0ba, 0x1f0ba + 40)) | set(range(bd.POINTS, bd.POINTS + 10))
    for a in mo.BIRD_LISTS:
        out |= set(range(a, a + 4 * len(mo.anim_list(sim, a)) + 4))
    return out


def _acid_bytes(run):
    """Phase 4b-3 tables (whole): spawn table pointers and entries per race (0xff end), the
    random path lists, every path up to its 0xff end, the acid pointer table, the animation
    lists (0xffffffff end) and the box bytes +4..+7 of their frames (0x1b12a type 0xe, the
    contact box 0x175c8)."""
    sim = run.sim
    out = set(range(ac.SPAWN_TABLE, ac.SPAWN_TABLE + 4 * md.LEVELS))
    out |= set(range(0x1f07a, 0x1f07a + 28))
    out |= set(range(ac.SINGLE[0], ac.GROUP[1] + 24))
    out |= _frame_bytes(sim, mo.ACID_LISTS, 4, 8)
    for a in mo.ACID_LISTS:
        out |= set(range(a, a + 4 * len(mo.anim_list(sim, a)) + 4))
    paths = [sim.l(t) for t in range(ac.SINGLE[0], ac.GROUP[1] + 24, 4)]
    for race in range(md.LEVELS):
        e = sim.l(ac.SPAWN_TABLE + 4 * race)
        while sim.prog[e] != 0xff:
            out |= set(range(e, e + 8))
            if sim.l(e + 2): paths.append(sim.l(e + 2))
            e += 8
        out.add(e)
    for p in paths:
        q = p
        while sim.prog[q] != 0xff:
            q += 4
        out |= set(range(p, q + 1))
    return out


def _slinky_bytes(run):
    """Phase 4b-2 tables (whole): spawn table pointers and entries per race (0xff end), every
    entry's waypoint list up to its 0xff end, the slinky pointer table, the animation lists
    (0xffffffff end) and the header bytes +0..+7 of their frames (occlusion 0x264aa, the
    boxes of 0x1b12a type 4 and the contact 0x14e92), the lunge height table."""
    sim = run.sim
    out = set(range(sl.SPAWN_TABLE, sl.SPAWN_TABLE + 4 * md.LEVELS))
    out |= set(range(0x1f006, 0x1f006 + 16)) | set(range(sl.REAR, sl.REAR + 0x20))
    out |= _frame_bytes(sim, mo.SLINKY_LISTS, 0, 8)
    for a in mo.SLINKY_LISTS:
        out |= set(range(a, a + 4 * len(mo.anim_list(sim, a)) + 4))
    for race in range(md.LEVELS):
        e = sim.l(sl.SPAWN_TABLE + 4 * race)
        while sim.prog[e] != 0xff:
            p = sim.l(e + 2)
            while sim.prog[p] != 0xff and sim.prog[p + 1] != 0xff:
                p += 4
            out |= set(range(sim.l(e + 2), p + 2)) | set(range(e, e + 8))
            e += 8
        out.add(e)
    return out


def _death_bytes(run):
    """Phase 4b-0 tables (whole): the player animation lists (0xffffffff end included), the
    reached shatter overlay entries (the remake reads the frame headers from them), the tables
    of DEATH_TABLES and the time out zone lists."""
    sim = run.sim
    out = set()
    for a in mo.DEATH_LISTS:
        out |= set(range(a, a + 4 * len(mo.anim_list(sim, a)) + 4))
    out |= set(range(ms.ANIM_SHARDS, ms.ANIM_SHARDS + 4 * run.death.shards))
    for a, length in _DEATH_TABLES:
        out |= set(range(a, a + length))
    for race in range(md.LEVELS):
        z = sim.l(0x24aae + 4 * race)
        end = z
        while sim.prog[end] != 0xff:
            end += 1
        out |= set(range(z, end + 1))
    return out


def _sprite_bytes(run):
    """Phase 4a tables (whole) as a byte address set: frame header box bytes +4..+7 of every
    frame of every mode 0/2 animation list (0x1b12a boxes of the draw list entries, tags
    drawn or not), the empty animation (0x20c14, its header read at
    0xffffffff + 4 = ROM 0x3-0x6), the tag 3 palette command bytes 0x1df18."""
    sim = run.sim
    out = set(range(3, 7)) | set(range(ms.ANIM_NONE, ms.ANIM_NONE + 4))
    out |= set(range(mo.TAG3_PAL, mo.TAG3_PAL + mo.TAG3_FRAMES))
    for race in range(md.LEVELS):
        for e, h in mo.box_headers(sim, race):
            out |= set(range(e, e + 4))
            if h != 0xffffffff: out |= set(range(h + 4, h + 8))
        for h in run.sprites[race].hdrs:
            out |= set(range(h + 4, h + 8))
    return out


def _merge(addrs, gap=8):
    """Sorted byte addresses -> [first, end) ranges, joining gaps up to gap bytes."""
    out = []
    for a in sorted(addrs):
        if out and a <= out[-1][1] + gap:
            out[-1][1] = max(out[-1][1], a + 1)
        else:
            out.append([a, a + 1])
    return out


def _table_bytes(run, descs):
    """Phase 2 tables (whole) as a byte address set."""
    sim = run.sim
    tables = list(_PROG_TABLES) + [(d, _DESC_BYTES) for d in descs]
    for n in range(md.LEVELS):
        for table, entry, last in ((0x1eb16, 6, lambda b: b[3] == 0xFF and b[4] == 0xFF),
                                   (0x1ef1a, 4, lambda b: b[0] == 0xFF)):
            a = sim.l(table + 4 * n)
            start = a
            while not last(sim.prog[a:a + entry]):
                a += entry
            tables.append((start, a + entry - start))
    covered = set()
    for a, length in tables:
        covered |= set(range(a, a + length))
    return covered


def _phase3_bytes(run):
    """Phase 3 tables (whole): level objects, occlusion (edge bitmaps are edge lists)."""
    out = set(od.object_tables(run.sim)) | od.occlusion_bytes(run.sim)
    for race in range(md.LEVELS):
        out |= od.object_bytes(run.sim, race)
    return out


def phase_bytes(run, terrains):
    """Program ROM bytes the remake reads, one set per phase: 2 physics, 3 objects and
    occlusion, 4a sprites, 4b-0 deaths, 4b-1 steelies, 4b-2 slinkies, 4b-3 acid, 4b-4a birds,
    tubes and shapes, 4b-4b minis, 4b-5 pistons. Whole tables, walked to their terminators;
    tests/test_games_marble.py replays every MAME fixture and race sweep on the model and
    fails on a read outside them."""
    return (_table_bytes(run, [t.desc for t in terrains]), _phase3_bytes(run), _sprite_bytes(run),
            _death_bytes(run), _steelie_bytes(run), _slinky_bytes(run), _acid_bytes(run),
            _bird_bytes(run) | _shape_bytes(run), _mini_bytes(run), _piston_bytes(run))


def model_bytes(terrains):
    """Bytes the model reads that the remake gets as fields, not blocks: the terrain
    descriptors (MarbleTerrainInfo top, range, cell; maxd, records and edges)."""
    out = set()
    for t in terrains:
        out |= set(range(t.desc, t.desc + _DESC_BYTES))
    return out


def blocks(plain, tables, skip):
    """[(address, bytes)] per phase, cumulative without skip: the blocks of the last phase are
    emitted."""
    out, cum = [], set()
    for t in tables:
        cum |= t - skip
        out.append([(a, bytes(plain[a:e])) for a, e in _merge(cum)])
    return out
