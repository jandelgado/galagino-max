"""Program ROM tables of the Marble Madness remake (MARBLE.md "Remake data").

The remake reads 68010 program ROM data by address (`Mem::romb`, blocks of marble_prog). This
module defines which bytes, one list of definitions per phase: typed tables (_Array, _Lists,
_Struct) emitted into marble_tables / marble_rom.h, Raw bytes still shipped as blocks, Model
bytes only the Python model reads. defs() is the single source for the emitted data and for
the coverage proof (tests/test_games_marble.py replays every MAME fixture and race sweep on
the model and fails on a read outside)."""
import struct
from dataclasses import dataclass
from types import MappingProxyType
from typing import Callable

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


_SIZE = MappingProxyType({"int8_t": 1, "uint8_t": 1, "int16_t": 2, "uint16_t": 2, "uint32_t": 4})
_PACK = MappingProxyType({"int8_t": "b", "uint8_t": "B", "int16_t": "h", "uint16_t": "H", "uint32_t": "I"})


@dataclass(frozen=True)
class _Array:
    """Table at a fixed address: MarbleTable<ctype, n> marble_<name> in marble_tables (native
    endian), element i < n checked at the site."""
    name: str
    addr: int
    ctype: str
    n: int
    what: str

    def bytes(self, sim):
        return set(range(self.addr, self.addr + self.n * _SIZE[self.ctype]))

    def data(self, sim):
        raw = bytes(sim.prog[self.addr:self.addr + self.n * _SIZE[self.ctype]])
        return struct.pack(f"<{self.n}{_PACK[self.ctype]}", *struct.unpack(f">{self.n}{_PACK[self.ctype]}", raw))


@dataclass(frozen=True)
class _Lists:
    """Lists behind a pointer table of count longs (one per race unless given): entries of
    entry bytes up to and including the one last() accepts. MarbleTable<uint8_t[entry], n>
    marble_<name> holds all entries, MarbleRange marble_<name>_of[k] the first and count of
    list k."""
    name: str
    table: int
    entry: int
    last: Callable[[bytes], bool]
    what: str
    count: int = md.LEVELS   # pointers in the table (per race unless given)

    def ranges(self, sim):
        """[(address, entries)] per list."""
        out = []
        for race in range(self.count):
            a = sim.l(self.table + 4 * race)
            n = 1
            while not self.last(sim.prog[a + self.entry * (n - 1):a + self.entry * n]):
                n += 1
            out.append((a, n))
        return out

    def bytes(self, sim):
        out = set(range(self.table, self.table + 4 * self.count))
        for a, n in self.ranges(sim):
            out |= set(range(a, a + self.entry * n))
        return out

    def data(self, sim):
        """(entries, [(first, n)] per race)."""
        data, where = bytearray(), []
        for a, n in self.ranges(sim):
            where.append((len(data) // self.entry, n))
            data += sim.prog[a:a + self.entry * n]
        return bytes(data), where


@dataclass(frozen=True)
class _Struct:
    """Named fields at offsets of a record: a literal `struct cname` constant marble_<name>."""
    name: str
    cname: str
    addr: int
    fields: tuple   # (field, offset, ctype)

    def bytes(self, sim):
        return {self.addr + o + k for _, o, c in self.fields for k in range(_SIZE[c])}

    def values(self, sim):
        return [struct.unpack(f">{_PACK[c]}", bytes(sim.prog[self.addr + o:self.addr + o + _SIZE[c]]))[0]
                for _, o, c in self.fields]


@dataclass(frozen=True)
class _Raw:
    """Bytes still shipped as address keyed blocks of marble_prog (Mem::romb)."""
    addrs: frozenset

    def bytes(self, sim):
        return self.addrs


@dataclass(frozen=True)
class _Keyed:
    """Bytes the remake reads by 68010 address (the work RAM holds the pointers): raw spans
    of one kind, MarbleSpans marble_<kind> (animEntry, frameBox, framePos ...)."""
    kind: str
    addrs: frozenset

    def bytes(self, sim):
        return self.addrs


@dataclass(frozen=True)
class _Model:
    """Bytes only the Python model reads: the remake has them as fields or lists elsewhere."""
    addr: int
    n: int

    def bytes(self, sim):
        return set(range(self.addr, self.addr + self.n))


def _raw(addr, n):
    return _Raw(frozenset(range(addr, addr + n)))


def _anim_lists(sim, lists):
    """Bytes of animation lists (longs up to and including the 0xffffffff end)."""
    out = set()
    for a in lists:
        out |= set(range(a, a + 4 * len(mo.anim_list(sim, a)) + 4))
    return out


_DESC_BYTES = 0x2e  # descriptor fields +0x00..+0x2d
_CAMERA = (("slow", 0, "uint8_t"), ("mid", 2, "uint8_t"), ("fast", 4, "uint8_t"), ("low", 6, "int16_t"),
           ("high", 8, "int16_t"), ("near", 10, "int16_t"), ("far", 12, "int16_t"))


def _physics(terrains):
    """Phase 2: physics, race start, respawn, camera, attract demo."""
    return [
        _Array("steps", 0x24b3a, "uint16_t", 21, "vertex steps"),   # 0x1caba: 4 x 3 words from word 0 (even diagonal) or 9 (odd)
        _Array("drop", 0x1ed62, "uint16_t", 32, "drop"),            # two level codes: drop per (code >> 7) & 0x1f
        _Array("push", 0x23d40, "int16_t", 8, "push"),              # 0x1706c edge push per cell fraction 0-7
        _Array("teeter", 0x2398c, "int8_t", 5, "teeter"),           # 0x160f6 lift by distance to the edge 0-4
        _Array("friction", 0x1eef8, "int16_t", 17, "friction"),     # 0x25e7c
        _Lists("zone_lines", 0x1eb16, 6, lambda b: b[3] == 0xFF and b[4] == 0xFF, "zone line"),
        _Array("goal_vx", 0x1eb2e, "int8_t", md.LEVELS, "goal vx"),
        _Array("goal_vy", 0x1eb34, "int8_t", md.LEVELS, "goal vy"),
        _Lists("checkpoints", 0x1ef1a, 4, lambda b: b[0] == 0xFF, "checkpoint"),   # respawn 0x2637a
        _Struct("camera_params", "MarbleCamera", 0x1f1ca, _CAMERA),      # 0x13ee6 speeds, sy thresholds and margins
        _Model(0x1f1ca, 14),                                       # the model reads the speeds as words
        _Model(0x2be00, 0x18),                                     # descriptor per race: MarbleTerrainInfo
        *[_Model(t.desc, _DESC_BYTES) for t in terrains],
        _Array("dyn_intermediate", 0x1d370, "uint16_t", 66, "dyn codes"),   # race start (0x11ac2)
        _Keyed("dyn", frozenset(range(0x1699c, 0x1699c + 0x16)) | frozenset(range(0x1b908, 0x1b908 + 0x1c))
               | frozenset(range(0x24e4e, 0x24e4e + 0xe4))),   # first dynamic code frames (0x13628: slot, count, words)
        _raw(0x1d364, 8), _raw(0x2419e, 0x88),                   # attract demo 0x1815a: waypoint list per demo race
        _Array("banner_chains", 0x242aa, "uint32_t", 7, "banner chain"),      # string chain per banner (BIOS 0x12a message)
    ]



def _steelies(run):
    """Phase 4b-1 tables (whole): spawn table pointers and entries per race (0xff end), every
    node list of an entry up to its 0xff, 0xff end, the object pointer table (targets), the
    pair list, the push table 0x24ad6 (|v high word| < 8), the score tables of k = 3."""
    sim = run.sim
    out = set(range(me.SPAWN_TABLE, me.SPAWN_TABLE + 4 * md.LEVELS))
    out |= set(range(me.PAIR_LIST, me.PAIR_LIST + 16)) | set(range(me.REPEL, me.REPEL + 8))
    for race in range(md.LEVELS):
        e = sim.l(me.SPAWN_TABLE + 4 * race)
        while sim.prog[e] != 0xff:
            p = sim.l(e + 2)
            while not (sim.prog[p] == 0xff and sim.prog[p + 1] == 0xff):
                p += 6
            out |= set(range(sim.l(e + 2), p + 2)) | set(range(e, e + 8))
            e += 8
        out.add(e)
    return [_Raw(frozenset(out)), _Array("obj_targets", me.OBJ_PTRS, "uint32_t", 4, "target")]   # P1, P2, steelie 0, 1


def _pistons(run):
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
    return [_Raw(frozenset(out)), _Keyed("anim", frozenset(_anim_lists(sim, mo.PISTON_LISTS)))]


def _frame_bytes(sim, lists, first, end):
    """Header bytes first..end - 1 of every frame of the animation lists."""
    out = set()
    for a in lists:
        for h in mo.anim_list(sim, a):
            out |= set(range(h + first, h + end))
    return out


_SHAPE_ENTRY = 16     # plane entry: normal (3 words), box (6 bytes), centre (3 bytes)
_SHAPE_LAST = 0x800   # normal x above: last entry of the shape


def _shapes():
    """Tube wall shapes 0x1cd00: the 7 shape pointers and the plane entries of every shape up
    to its last (normal x > 0x800)."""
    return _Lists("shapes", ms.SHAPES, _SHAPE_ENTRY, lambda b: ms.s16(b[0] << 8 | b[1]) > _SHAPE_LAST,
                  "shape plane", count=7)


def _minis(run):
    """Phase 4b-4b tables (whole): the mini pointer table, the step tables 0x244b6/0x244d6,
    the cell bitmap 0x24496, the animation lists (0xffffffff end) and the draw list box
    bytes +4..+7 of their frames (0x1b12a types 7-9)."""
    sim = run.sim
    out = set(range(mn.GRID, mn.GRID + 32))
    out |= set(range(mn.DX, mn.DX + 32)) | set(range(mn.DY, mn.DY + 32))
    return [_Raw(frozenset(out)), _Keyed("anim", frozenset(_anim_lists(sim, mo.MINI_LISTS))),
            _Keyed("frame", frozenset(_frame_bytes(sim, mo.MINI_LISTS, 4, 8))),
            _Array("obj_minis", 0x1f096, "uint32_t", 9, "mini")]


def _birds(run):
    """Phase 4b-4a tables (whole): the bird pointer table, the spawn cells, the animation
    lists (0xffffffff end)."""
    sim = run.sim
    out = set(range(bd.POINTS, bd.POINTS + 10))
    return [_Raw(frozenset(out)), _Keyed("anim", frozenset(_anim_lists(sim, mo.BIRD_LISTS))),
            _Array("obj_birds", 0x1f0ba, "uint32_t", 10, "bird")]


def _acid(run):
    """Phase 4b-3 tables (whole): spawn table pointers and entries per race (0xff end), the
    random path lists, every path up to its 0xff end, the acid pointer table, the animation
    lists (0xffffffff end) and the box bytes +4..+7 of their frames (0x1b12a type 0xe, the
    contact box 0x175c8)."""
    sim = run.sim
    out = set(range(ac.SPAWN_TABLE, ac.SPAWN_TABLE + 4 * md.LEVELS))
    out |= set(range(ac.SINGLE[0], ac.GROUP[1] + 24))
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
    return [_Raw(frozenset(out)), _Keyed("anim", frozenset(_anim_lists(sim, mo.ACID_LISTS))),
            _Keyed("frame", frozenset(_frame_bytes(sim, mo.ACID_LISTS, 4, 8))),
            _Array("obj_acids", 0x1f07a, "uint32_t", 7, "acid")]


def _slinkies(run):
    """Phase 4b-2 tables (whole): spawn table pointers and entries per race (0xff end), every
    entry's waypoint list up to its 0xff end, the slinky pointer table, the animation lists
    (0xffffffff end) and the header bytes +0..+7 of their frames (occlusion 0x264aa, the
    boxes of 0x1b12a type 4 and the contact 0x14e92), the lunge height table."""
    sim = run.sim
    out = set(range(sl.SPAWN_TABLE, sl.SPAWN_TABLE + 4 * md.LEVELS))
    out |= set(range(sl.REAR, sl.REAR + 0x20))
    for race in range(md.LEVELS):
        e = sim.l(sl.SPAWN_TABLE + 4 * race)
        while sim.prog[e] != 0xff:
            p = sim.l(e + 2)
            while sim.prog[p] != 0xff and sim.prog[p + 1] != 0xff:
                p += 4
            out |= set(range(sim.l(e + 2), p + 2)) | set(range(e, e + 8))
            e += 8
        out.add(e)
    return [_Raw(frozenset(out)), _Keyed("anim", frozenset(_anim_lists(sim, mo.SLINKY_LISTS))),
            _Keyed("frame", frozenset(_frame_bytes(sim, mo.SLINKY_LISTS, 0, 8))),
            _Array("obj_slinkies", 0x1f006, "uint32_t", 4, "slinky")]


def _deaths(run):
    """Phase 4b-0: the player animation lists (0xffffffff end included), the reached shatter
    overlay entries (the remake reads the frame headers from them), the tables the death and
    score code index (whole; the fixtures read only parts)."""
    sim = run.sim
    anim = _anim_lists(sim, mo.DEATH_LISTS)
    anim |= set(range(ms.ANIM_SHARDS, ms.ANIM_SHARDS + 4 * run.death.shards))
    return [
        _Keyed("anim", frozenset(anim)),
        _Array("sine", ms.SINE, "int16_t", 0x66, "sine"),            # sine_pair 0..0x64, sine_lerp i + 1 <= 0x65
        _Model(ms.LERP, 2 * 0x35),                                   # lerp_1ee6e: i = (arg >> 10) - 0xc, i + 1 <= 0x34
        _Model(ms.DOT_ANGLE, 2 * 39),                                # dots_1c014: word 3 * ady + dx, ady <= 0xc, dx <= 2
        _Model(ms.DOT_DY, 13),                                       # dots_1c014: ady <= 0xc
        _Array("star_steps", ms.ORBIT_STEPS, "int8_t", 16, "star step"),   # 8 x (dx, dy) sparkle star offsets
        _Model(0x1d3f4, 16),                                         # dots_init_25b40 rest pose x, y
        _Array("bonus_limit", 0x24a94, "uint8_t", md.LEVELS, "bonus limit"),   # timeout_1bbaa rng limit per race
        _Lists("bonus_zones", 0x24aae, 1, lambda b: b[0] == 0xFF, "bonus zone"),   # timeout_1bbaa zones per race
        _Array("scores", ms.SCORES, "int16_t", 17, "score"),         # score_285b0 points: goal (race * 2 + 3), time bonus (0x10)
        _Array("score_texts", ms.SCORE_TEXTS, "uint32_t", 17, "score text"),   # score_285b0 score texts
    ]


def _sprites(run):
    """Phase 4a: the draw list entry longs and frame header box bytes +4..+7 of every frame of
    every mode 0/2 animation list (0x1b12a boxes, tags drawn or not), the sprite frames of the
    race sets, the empty animation (0x20c14; its header 0xffffffff + 4 = ROM 0x3-0x6, zeros
    for the remake: frameBox), the tag 3 palette command bytes 0x1df18."""
    sim = run.sim
    anim, frame = set(range(ms.ANIM_NONE, ms.ANIM_NONE + 4)), set()
    for race in range(md.LEVELS):
        for e, h in mo.box_headers(sim, race):
            anim |= set(range(e, e + 4))
            if h != 0xffffffff: frame |= set(range(h + 4, h + 8))
        for h in run.sprites[race].hdrs:
            frame |= set(range(h + 4, h + 8))
    return [_Keyed("anim", frozenset(anim)), _Keyed("frame", frozenset(frame)), _Model(3, 4),
            _Array("tag3_pal", mo.TAG3_PAL, "uint8_t", mo.TAG3_FRAMES, "tag 3 palette")]


def _merge(addrs, gap=8):
    """Sorted byte addresses -> [first, end) ranges, joining gaps up to gap bytes."""
    out = []
    for a in sorted(addrs):
        if out and a <= out[-1][1] + gap:
            out[-1][1] = max(out[-1][1], a + 1)
        else:
            out.append([a, a + 1])
    return out


def _objects(run):
    """Phase 3: level objects (scripts, animation lists, terrain code blocks, spawn lists and
    groups, slot addresses, the intermediate wave tables 0x1d06a), occlusion (edge bitmaps are
    edge lists)."""
    sim = run.sim
    code, anim, dyn = set(), od.global_anim_bytes(sim), set()
    for race in range(md.LEVELS):
        c, a, d = od.object_parts(sim, race)
        code |= c
        anim |= a
        dyn |= d
    return [
        _Keyed("script", frozenset(code)), _Keyed("anim", frozenset(anim)), _Keyed("dyn", frozenset(dyn)),
        _Lists("spawn_lists", 0x1dec0, 6, lambda b: b[0] == 0xFF, "spawn entry"),   # lo, hi, script
        _Array("spawn_groups", 0x1ded8, "uint32_t", 16, "spawn group"),             # 4 groups of 4 scripts
        _Array("slot_addr", 0x1f016, "uint32_t", 25, "slot"),                       # 0x12d6e slot addresses
        _Array("wave", 0x24c7a, "uint8_t", 0x24e28 - 0x24c7a, "wave table"),        # 0x1d06a tables, by offset
        # occlusion 0x264aa: shape diagonal offsets (3 x 5 per column), flat entry code and y by
        # height difference, quadrant masks and vertex offsets of 0x177f8, the occluder lists
        _Array("occ_shapes", 0x1eaba, "int8_t", 15, "occlusion shape"),
        _Array("flat_h", 0x1ead2, "uint16_t", 17, "flat code"),
        _Array("flat_y", 0x1eaf4, "uint16_t", 17, "flat y"),
        _Array("quad_masks", 0x24176, "uint16_t", 4, "quadrant mask"),
        _Array("quads", 0x2417e, "uint32_t", 8, "quadrant"),
        _Array("occluder_tabs", 0x1ea2a, "uint32_t", 36, "occluder table"),   # per column: 6 word offsets per race
        _Keyed("occluder", frozenset(od.occluder_bytes(sim))),                # the tables and records (occluderWord)
    ]


def defs(run, terrains):
    """Program ROM data the remake reads, one list of definitions per phase: 2 physics, 3
    objects and occlusion, 4a sprites, 4b-0 deaths, 4b-1 steelies, 4b-2 slinkies, 4b-3 acid,
    4b-4a birds, tubes and shapes, 4b-4b minis, 4b-5 pistons. Whole tables, walked to their
    terminators; tests/test_games_marble.py replays every MAME fixture and race sweep on the
    model and fails on a read outside them."""
    return (_physics(terrains), _objects(run), _sprites(run), _deaths(run),
            _steelies(run), _slinkies(run), _acid(run),
            _birds(run) + [_shapes()], _minis(run), _pistons(run))


def phase_bytes(run, terrains):
    """Byte address set per phase of defs()."""
    return tuple(set().union(*(d.bytes(run.sim) for d in phase)) for phase in defs(run, terrains))


def blocks(plain, phases):
    """[(address, bytes)] of the Raw definitions per phase, cumulative: the blocks of the last
    phase are emitted as marble_prog."""
    out, cum = [], set()
    for phase in phases:
        for d in phase:
            if isinstance(d, _Raw):
                cum |= d.addrs
        out.append([(a, bytes(plain[a:e])) for a, e in _merge(cum)])
    return out


def emit(f, sim, phases, add):
    """Typed definitions of every phase into marble_rom.h; add(data, align) appends to
    marble_tables and returns the offset. Returns the table bytes per phase."""
    sizes, keyed = [], {}
    for phase in phases:
        size = 0
        for d in phase:
            if isinstance(d, _Keyed):
                keyed[d.kind] = keyed.get(d.kind, frozenset()) | d.addrs
                size += len(d.addrs)
            elif isinstance(d, _Array):
                data = d.data(sim)
                print(f"static const MarbleTable<{d.ctype}, {d.n}> marble_{d.name} = "
                      f"{{ {add(data, _SIZE[d.ctype])}, \"{d.what}\" }};  // 0x{d.addr:05x}", file=f)
                size += len(data)
            elif isinstance(d, _Lists):
                data, where = d.data(sim)
                print(f"static const MarbleTable<uint8_t[{d.entry}], {len(data) // d.entry}> marble_{d.name} = "
                      f"{{ {add(data, 1)}, \"{d.what}\" }};  // 0x{d.table:05x}: lists per race, terminator included",
                      file=f)
                print(f"static const MarbleRange marble_{d.name}_of[{d.count}] = {{ "
                      + ", ".join(f"{{ {first}, {n} }}" for first, n in where) + " };", file=f)
                size += len(data) + 4 * d.count
            elif isinstance(d, _Struct):
                print(f"struct {d.cname} {{ " + " ".join(f"{c} {n};" for n, _, c in d.fields) + " };", file=f)
                print(f"static const {d.cname} marble_{d.name} = {{ "
                      + ", ".join(str(v) for v in d.values(sim)) + f" }};  // 0x{d.addr:05x}", file=f)
                size += sum(_SIZE[c] for _, _, c in d.fields)
        sizes.append(size)
    for kind, addrs in sorted(keyed.items()):
        spans = [(a, e, add(sim.prog[a:e], 1)) for a, e in _merge(addrs, 0)]
        print(f"static const MarbleSpan marble_{kind}_spans[{len(spans)}] = {{\n  "
              + ",\n  ".join(f"{{ 0x{a:05x}, {e - a}, {off} }}" for a, e, off in spans) + "\n};", file=f)
        print(f"static const MarbleSpans marble_{kind} = {{ marble_{kind}_spans, {len(spans)}, "
              f"\"{kind}\" }};", file=f)
    return sizes
