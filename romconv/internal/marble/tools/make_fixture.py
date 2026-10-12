#!/usr/bin/env python3
"""Marble physics host test fixture from MAME traces (tools/mame_trace.lua).

A segment starts at one trace snapshot and replays the player update with marble_sim.py
using only the initial work RAM blocks below: no level object slots (Phase 4), no per tick
copies of dynamic terrain codes or enemy scratch words. Positions must equal the trace every
tick; the segment ends at the death the sim detects (its frame gap in the trace confirms it).

  make_fixture.py search TRACE RACE [MIN]   list reproducible segments ending in a death
  make_fixture.py write OUT RACE:TRACE:FRAME:TICKS ...
                                            write the C header (marble_trace.h)
  make_fixture.py osearch TRACE RACE [MIN]  list reproducible object segments (slots,
                                            dynamic codes, occlusion masks) and their events
  make_fixture.py owrite OUT RACE:TRACE:FRAME:TICKS ...
                                            write the object header
                                            (test_marble_objects/marble_objects_trace.h)
  make_fixture.py ssearch TRACE RACE [MIN]  list reproducible sprite segments (objects, draw
                                            list, published objects) and their events
  make_fixture.py swrite OUT RACE:TRACE:FRAME:TICKS ...
                                            write the sprites header
                                            (test_marble_sprites/marble_sprites_trace.h)
  make_fixture.py dwrite OUT RACE:TRACE:FRAME:TICKS ...
                                            death segments of tick start traces
                                            (test_marble_deaths/marble_deaths_trace.h)
  make_fixture.py stwrite OUT RACE:TRACE:FRAME:TICKS[:OBJ:X:Y:Z] ...
                                            steelie segments of tick start traces, optional
                                            MARBLE_DROP at FRAME
                                            (test_marble_steelies/marble_steelies_trace.h)
  make_fixture.py slwrite OUT RACE:TRACE:FRAME:TICKS ...
                                            slinky segments of tick start traces
                                            (test_marble_slinkies/marble_slinkies_trace.h)
  make_fixture.py acwrite OUT RACE:TRACE:FRAME:TICKS ...
                                            acid segments of tick start traces
                                            (test_marble_acid/marble_acid_trace.h)
  make_fixture.py mnwrite OUT RACE:TRACE:FRAME:TICKS ...
                                            silly mini segments of tick start traces
                                            (test_marble_minis/marble_minis_trace.h)
  make_fixture.py pswrite OUT RACE:TRACE:FRAME:TICKS ...
                                            aerial piston segments of tick start traces
                                            (test_marble_pistons/marble_pistons_trace.h)
  make_fixture.py twrite OUT RACE:TRACE:FRAME:TICKS|KEEP.h ...
                                            tube segments, RNG as input, all enemies
                                            (test_marble_deaths/marble_tubes_trace.h)
  make_fixture.py awrite OUT RACE:TRACE:FRAME:TICKS ...
                                            aerial object segments, all enemies
                                            (test_marble_aerial/marble_aerial_trace.h)
  make_fixture.py bdwrite OUT RACE:TRACE:FRAME:TICKS ...
                                            silly bird segments of tick start traces
                                            (test_marble_birds/marble_birds_trace.h)
  make_fixture.py tbwrite OUT RACE:TRACE:FRAME:TICKS ...
                                            time bonus (state 8) segments, all enemies
                                            (test_marble_flow/marble_bonus_trace.h)
  make_fixture.py fwrite OUT FRAME_TRACE ...
                                            game flow: race end sequences, next race
                                            carry, time out (test_marble_flow/marble_flow.h)
  make_fixture.py rwrite OUT               model runs from the remake race start, all races:
                                            stick input, goal crossing
                                            (test_marble_deaths/marble_start_trace.h)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import marble_sim as ms
import marble_mo as mo

P = ms.P
TICK_FRAMES = 2       # logic tick = 2 frames during a race
SLOTS = (0xa9c, 0xa9c + 25 * 0x56)
# initial RAM blocks (68010 address, length): object, race words, scratch globals, dynamic
# terrain codes, vertex heights (MARBLE.md "Marble physics"). Only blocks the update reads
# before writing are emitted.
BLOCKS = ((P, 0xe2), (0x400390, 6), (0x400660, 0x48), (ms.DYN, 0x84), (0x401c28, 0x20))
# object bytes that must equal the trace every tick (more than the fixture stores, so hidden
# state such as +0x36 cannot drift): velocity, position, state, zone, ground/edge flags,
# dizzy counters, surface code
CHECK = tuple(range(0x18)) + (0x1a, 0x1b, 0x36, 0x37, 0x56, 0x57, 0x58)


class RecMem(ms.Mem):
    """Mem that records work RAM bytes read before the run writes them."""
    def __init__(self, terrain, ram):
        super().__init__(terrain, ram)
        self.written = set()
        self.first_read = set()
    def rb(self, a):
        o = (a & 0xffffff) - 0x400000
        if 0 <= o < 0x2000 and o not in self.written:
            self.first_read.add(o)
        return super().rb(a)
    def wb(self, a, v):
        self.written.add((a & 0xffffff) - 0x400000)
        super().wb(a, v)


def run(t, trace, i, m, limit=10 ** 6):
    """Replay from snapshot i without overrides. Returns (ticks, end, stats)."""
    ticks = []
    stats = {"air": 0, "teeter": 0, "land": 0, "wall": 0, "side": 0, "left": 0,
             "vx<0": 0, "vy<0": 0}
    for (fr, r, _), (fr2, r2, _) in zip(trace[i:], trace[i + 1:]):
        if len(ticks) == limit:
            return ticks, "limit", stats
        c6, c7 = ms.tb(r2, P + 0xc6), ms.tb(r2, P + 0xc7)
        sx0 = ms.s16(m.rw(P + 0x10) - m.rw(P + 0xc))
        m.events = []; m.sounds = []
        ms.tick(m, c6, c7)
        pos = (m.sl(P + 0xc), m.sl(P + 0x10), m.sl(P + 0x14))
        state = m.rb(P + 0x1a)
        ticks.append((ms.s8(c6), ms.s8(c7)) + pos + (state,))
        if any(e in ms.DEATH for e in m.events):
            return ticks, ("death %s, trace gap %d frames" % (m.events[-1], fr2 - fr)
                           if fr2 - fr != TICK_FRAMES else "unconfirmed death"), stats
        if fr2 - fr != TICK_FRAMES or any(m.rb(P + o) != ms.tb(r2, P + o) for o in CHECK):
            ticks.pop()
            return ticks, "diverge at frame %d" % fr, stats
        stats["air"] += m.rb(P + 0x36) == 2
        stats["teeter"] += m.rb(P + 0x36) == 1
        stats["land"] += any(isinstance(e, tuple) and e[0] == "land" for e in m.events)
        stats["wall"] += 0x42 in m.sounds
        stats["side"] += "side" in m.events
        stats["left"] += ms.s16(m.rw(P + 0x10) - m.rw(P + 0xc)) < sx0
        stats["vx<0"] += m.sl(P) < 0
        stats["vy<0"] += m.sl(P + 4) < 0
    return ticks, "trace end", stats


def snapshot_ram(r):
    ram = bytearray(r)
    ram[SLOTS[0]:SLOTS[1]] = bytes(SLOTS[1] - SLOTS[0])
    return ram


def segment(t, trace, i, limit=10 ** 6):
    """Run from snapshot i, then again from only the RAM blocks it read: both must agree."""
    m = RecMem(t, snapshot_ram(trace[i][1]))
    ticks, end, stats = run(t, trace, i, m, limit)
    reads = {o for o in m.first_read if not SLOTS[0] <= o < SLOTS[1]}
    blocks = []
    for addr, n in BLOCKS:
        o = addr - 0x400000
        if any(o <= k < o + n for k in reads):
            blocks.append((addr, bytes(trace[i][1][o:o + n])))
            reads -= set(range(o, o + n))
    if reads:
        raise SystemExit("RAM read outside the fixture blocks: %s"
                         % sorted(hex(0x400000 + k) for k in reads))
    seg = {"race": t.race, "frame": trace[i][0], "ram": blocks, "ticks": ticks}
    if ms.replay_fixture(seg, ms.fixture_mem(seg, t)) is not None:
        raise SystemExit("segment at frame %d does not replay from its RAM blocks" % seg["frame"])
    return seg, end, stats


def cmd_search(path, race, need):
    t = ms.Terrain(race)
    trace = ms.load_trace(path)
    i = 0
    while i < len(trace) - 1:
        if not ms.live(trace[i][1]):
            i += 1; continue
        seg, end, stats = segment(t, trace, i)
        n = len(seg["ticks"])
        if n >= need and end.startswith("death "):
            print("frame %d: %d ticks, %s, %s" % (seg["frame"], n, end, stats))
            i += n
        else:
            i += 10


# ---------------------------------------------------------------- objects fixture

# initial RAM blocks of an object segment: as above, plus the whole slot area and all 128
# dynamic code words (their crc32 are checked), plus whatever else the run reads first
# (slot globals, camera, projection scratch), merged with gaps up to 16 bytes
OBJ_BLOCKS = ((P, 0xe2), (0x400390, 6), (0x400660, 0x48), (0x401c28, 0x20),
              (0x400000 + ms.OBJ_DYN_AREA[0], ms.OBJ_DYN_AREA[1] - ms.OBJ_DYN_AREA[0]),
              (0x400000 + ms.OBJ_SLOT_AREA[0], ms.OBJ_SLOT_AREA[1] - ms.OBJ_SLOT_AREA[0]))


def obj_run(t, trace, i, limit=10 ** 6, sp=None):
    """Replay from snapshot i with objects and sprites on, nothing copied from later
    snapshots except the trackball deltas and the camera long (inputs of the tick). The run
    ends before the first tick whose player, slots, dynamic codes or masks differ from the
    trace, near an enemy (enemies are not ported), at a frame gap. Returns (ticks, end, events, reads)."""
    m = RecMem(t, trace[i][1])
    m.objects = m.sprites = True
    if sp: m.drawlist = mo.DrawList()
    ticks = []
    ev = {"lists": 0, "ins": 0, "rem": 0, "reorder": 0, "wall": 0, "dyn": 0, "masks": 0, "hflip": 0, "tall": 0, "hidden": 0, "tag3": 0,
          "post": 0}
    prev_dyn = bytes(m.ram[slice(*ms.OBJ_DYN_AREA)])
    a = P - 0x400000
    for (fr, r, _), (fr2, r2, _) in zip(trace[i:], trace[i + 1:]):
        if len(ticks) == limit: return ticks, "limit", ev, m.first_read
        if not ms.live(r) or not ms.live(r2): return ticks, "not live", ev, m.first_read
        if ms.enemy_near(r, r2): return ticks, "enemy near", ev, m.first_read
        if fr2 - fr != TICK_FRAMES and not (sp and fr2 - fr == TICK_FRAMES + 1):
            return ticks, "frame gap", ev, m.first_read     # sprites: a one frame lag is fine
        c6, c7 = ms.tb(r2, P + 0xc6), ms.tb(r2, P + 0xc7)
        cam = ms.tl(r2, 0x40097c) & 0xffffffff
        m.events = []; m.sounds = []
        snap = (bytes(m.ram), set(m.first_read), set(m.written))
        cam_before = m.rw(0x40097e)
        lst0 = mo.draw_list(m) if sp else None
        ms.tick(m, c6, c7, camera=cam)
        if sp: mo.refresh_sort(m)
        got = m.ram[a + 0x1c:a + 0x56]; ref = r2[a + 0x1c:a + 0x56]
        bad = (any(m.sl(P + o) != ms.tl(r2, P + o) for o in (0xc, 0x10, 0x14))
               or any(m.rb(P + o) != ms.tb(r2, P + o) for o in CHECK)
               or ms.slot_diff(m, r2) or got[:0x0e] != ref[:0x0e] or got[0x1c:] != ref[0x1c:])
        why = "differs at frame %d" % fr2
        if not bad and sp and mo.draw_list(m) != mo.draw_list(mo.Ram(r2)):
            bad, why = True, "draw list differs at frame %d" % fr2
        if bad:
            m.ram[:], m.first_read, m.written = bytearray(snap[0]), snap[1], snap[2]
            return ticks, why, ev, m.first_read
        st = ms.obj_state(m)
        tk = (ms.s8(c6), ms.s8(c7), cam) + st[:5] + (st[5], st[6], st[7])
        if sp:
            pub = mo.publish(m, sp, cam_before)
            top = ms.s16(cam_before) - m.sw(m.t.desc + 0x10)
            if not mo.check_publish(mo.Ram(bytes(m.ram)), sp, pub[0], top):
                m.ram[:], m.first_read, m.written = bytearray(snap[0]), snap[1], snap[2]
                return ticks, "published objects differ from the MO model at frame %d" % fr2, \
                    ev, m.first_read
            tk += (pub,)
            lst = mo.draw_list(m)
            ev["lists"] += 1
            ev["ins"] += len(lst) > len(lst0)
            ev["rem"] += len(lst) < len(lst0)
            ev["reorder"] += len(lst) == len(lst0) and lst != lst0
        ticks.append(tk)
        dyn = bytes(m.ram[slice(*ms.OBJ_DYN_AREA)])
        ev["dyn"] += dyn != prev_dyn; prev_dyn = dyn
        ev["wall"] += any(isinstance(e, tuple) and e[0] == "slot wall" for e in m.events)
        ev["post"] += 0x42 in m.sounds and not any(
            isinstance(e, tuple) and e[0] == "slot wall" for e in m.events)
        ev["hidden"] += st[4] == 0
        ents = [struct.unpack(">H", st[5][6 * k:6 * k + 2])[0] for k in range(5)]
        ents = [e for e in ents if e]
        ev["masks"] += bool(ents)
        ev["hflip"] += any(e & 0x8000 for e in ents)
        ev["tall"] += any((e >> 11) & 7 for e in ents)
        ev["tag3"] += any(m.rb(ms.slot(k) + 0x18) and m.rb(ms.slot(k) + 0x1f) == 3
                          for k in range(ms.SLOT_N))
    return ticks, "trace end", ev, m.first_read


def obj_blocks(trace_ram, reads):
    """Initial RAM blocks: OBJ_BLOCKS plus the other first-read bytes."""
    keep = set()
    for addr, n in OBJ_BLOCKS:
        keep.update(range(addr - 0x400000, addr - 0x400000 + n))
    extra = sorted(reads - keep)
    ranges = []
    for o in extra:
        if ranges and o <= ranges[-1][1] + 16: ranges[-1][1] = o + 1
        else: ranges.append([o, o + 1])
    blocks = sorted([(addr - 0x400000, addr - 0x400000 + n) for addr, n in OBJ_BLOCKS]
                    + [tuple(r) for r in ranges])
    merged = []
    for lo, hi in blocks:
        if merged and lo <= merged[-1][1]: merged[-1][1] = max(merged[-1][1], hi)
        else: merged.append([lo, hi])
    return [(0x400000 + lo, bytes(trace_ram[lo:hi])) for lo, hi in merged]


def obj_segment(t, trace, i, limit=10 ** 6):
    ticks, end, ev, reads = obj_run(t, trace, i, limit)
    seg = {"race": t.race, "frame": trace[i][0], "ram": obj_blocks(trace[i][1], reads),
           "ticks": ticks}
    return seg, end, ev


def obj_check(seg):
    """Replay from the initial RAM blocks alone."""
    return ms.replay_objfixture(seg, ms.fixture_mem(seg, ms.Terrain(seg["race"])))


def cmd_osearch(path, race, need):
    t = ms.Terrain(race)
    trace = ms.load_trace(path)
    i = 0
    while i < len(trace) - 1:
        if not ms.live(trace[i][1]):
            i += 1; continue
        ticks, end, ev, _ = obj_run(t, trace, i)
        if len(ticks) >= need:
            print("frame %d: %d ticks, %s, %s" % (trace[i][0], len(ticks), end, ev))
        i += max(1, len(ticks) // 4) if len(ticks) >= need else max(1, len(ticks))


# ---------------------------------------------------------------- sprites fixture

# draw list entries, list and globals, tag 3 global frame pointers and timers: always blocks
SPR_BLOCKS = OBJ_BLOCKS + ((mo.ENTRIES, 0x3e4 - 0x1dc), (0x40044a, 0x12))


def sprite_blocks(trace_ram, reads):
    """Initial RAM blocks of a sprite segment (obj_blocks with the draw list added)."""
    global OBJ_BLOCKS
    keep = OBJ_BLOCKS
    OBJ_BLOCKS = SPR_BLOCKS
    try:
        return obj_blocks(trace_ram, reads)
    finally:
        OBJ_BLOCKS = keep


def sprite_segment(t, trace, i, sp, limit=10 ** 6):
    ticks, end, ev, reads = obj_run(t, trace, i, limit, sp)
    seg = {"race": t.race, "frame": trace[i][0], "ram": sprite_blocks(trace[i][1], reads),
           "ticks": [(c6, c7, cam, k[-1]) for (c6, c7, cam, *k) in ticks]}
    return seg, end, ev


def sprite_events(seg, sp):
    """Event counts of a segment's published lists (lift = set 6, flags set 3, waves and
    strips the intermediate tag 6 sets)."""
    ev = {"lift over marble": 0, "marble over lift": 0, "flags": 0, "waves": 0,
          "strips": 0, "max objs": 0, "ticks": len(seg["ticks"])}
    for *_, (objs, at, variant) in seg["ticks"]:
        ev["max objs"] = max(ev["max objs"], len(objs))
        lift = [k for k, o in enumerate(objs) if o[1] == 6]
        ev["marble over lift"] += any(k < at for k in lift)
        ev["lift over marble"] += any(k >= at for k in lift)
        ev["flags"] += any(o[1] == 3 for o in objs)
        ev["waves"] += any(o[1] == 5 or (o[1] == 7 and sp.race == 2) for o in objs)
        ev["strips"] += any(o[1] == mo.SET_SHADE for o in objs)
    return ev


def cmd_ssearch(path, race, need):
    t = ms.Terrain(race)
    sp = mo.Sprites(race)
    trace = ms.load_trace(path)
    i = 0
    while i < len(trace) - 1:
        if not ms.live(trace[i][1]):
            i += 1; continue
        seg, end, ev = sprite_segment(t, trace, i, sp)
        n = len(seg["ticks"])
        if n >= need:
            print("frame %d: %d ticks, %s, %s, %s" % (trace[i][0], n, end, ev,
                                                      sprite_events(seg, sp)))
        i += max(1, n // 4) if n >= need else max(1, n)


def sprite_check(seg, sp):
    """Replay from the initial RAM blocks alone: the published lists must equal."""
    m = ms.fixture_mem(seg, ms.Terrain(seg["race"]))
    flat = [o for *_, (objs, at, v) in seg["ticks"] for o in objs]
    first, ticks = 0, []
    for c6, c7, cam, (objs, at, v) in seg["ticks"]:
        ticks.append((c6, c7, cam, len(objs), at, v, first)); first += len(objs)
    chk = {"race": seg["race"], "ticks": ticks, "objs": flat}
    return mo.replay_spritefixture(chk, m, sp)[0]


def cmd_swrite(out, specs):
    segs = []
    for spec in specs:
        race, path, frame, n = spec.split(":")
        race, frame, n = int(race), int(frame), int(n)
        trace = ms.load_trace(path)
        sp = mo.Sprites(race)
        i = next(k for k, x in enumerate(trace) if x[0] == frame)
        seg, end, ev = sprite_segment(ms.Terrain(race), trace, i, sp, n)
        if len(seg["ticks"]) != n:
            raise SystemExit("race %d frame %d: only %d ticks (%s)" % (race, frame,
                                                                       len(seg["ticks"]), end))
        bad = sprite_check(seg, sp)
        if bad is not None:
            raise SystemExit("race %d frame %d: does not replay from its RAM blocks (tick %d)"
                             % (race, frame, bad))
        print("race %d frame %d: %d ticks, %s, %s, RAM %s" % (race, frame, n, ev,
              sprite_events(seg, sp), ", ".join("%06x+%d" % (a, len(b)) for a, b in seg["ram"])))
        segs.append(seg)
    with open(out, "w") as f:
        w = lambda s="": print(s, file=f)
        w("// Generated by romconv/internal/marble/tools/make_fixture.py swrite; do not edit.")
        w("// Marble level object sprite host test: MAME trace segments (tools/mame_trace.lua)")
        w("// between MARBLE_PLACE teleports. The reference models marble_sim.py (objects, draw")
        w("// list hooks) and marble_mo.py (box refresh and sort of 0x26f3e, publish) reproduce")
        w("// them from the initial work RAM blocks and the inputs alone. Derived numbers only.")
        w("// Replay check:")
        w("//   python3 romconv/internal/marble/tools/marble_mo.py spritefixture <this file>")
        w("#ifndef MARBLE_SPRITES_TRACE_H\n#define MARBLE_SPRITES_TRACE_H\n#include <stdint.h>\n")
        w("// Work RAM 0x400000-0x401fff is 0 except these blocks (68010 address, big-endian):")
        w("// the player, race words, scratch, the 25 slots, the dynamic codes, the MO draw list")
        w("// entries 0x4001dc-0x4003e3 (list 0x4003bc), the tag 3 globals and what a run reads first.")
        w("struct MarbleSprRam { uint32_t addr; uint16_t len; const uint8_t *bytes; };")
        w("// One published object: the frame id of the race (marble_sprites[], sorted header")
        w("// addresses, then the 23 wave strip entries), palette set (3, 5, 6, 7; 8 = wave strip")
        w("// shade), x and y of the slot origin in race pixels (x = sx, tag 5: sx - 1; y = camera")
        w("// - descriptor +0x10 + 240 - sy; column offsets come from the frame).")
        w("struct MarbleSprObj { uint8_t frame, set; int16_t x, y; };")
        w("// One logic tick as tick(c6, c7, camera) of marble_sim.py (player update, slot update,")
        w("// queued frame, then the camera long 0x40097c, an input like the trackball deltas),")
        w("// then 0x26f3e: boxes refreshed, three sort passes. n published objects objs[first ..")
        w("// first + n) in draw list order (back to front) with the arcade culls (margin 0), made")
        w("// with the camera word 0x40097e the slot update saw (before the camera long is stored);")
        w("// marble_at: objects drawn before the marble (n if the marble is not in the list);")
        w("// variant: tag 3 palette variant 0-3 of the drawn lift frame (0xff: none this tick).")
        w("struct MarbleSprTick {")
        w("  int8_t c6, c7; uint32_t camera; uint8_t n, marble_at, variant; uint16_t first;")
        w("};")
        w("struct MarbleSprSegment {")
        w("  uint8_t race; uint32_t frame;  // race 0-5, MAME frame of the first snapshot")
        w("  uint8_t nram; const MarbleSprRam *ram; uint16_t nticks; const MarbleSprTick *ticks;")
        w("  uint16_t nobjs; const MarbleSprObj *objs;")
        w("};\n")
        for k, seg in enumerate(segs):
            p = "marble_spr_s%d" % k
            for j, (addr, b) in enumerate(seg["ram"]):
                w("static const uint8_t %s_ram%d[] = {\n%s\n};" % (p, j, c_bytes(b)))
            w("static const MarbleSprRam %s_ram[] = {" % p)
            for j, (addr, b) in enumerate(seg["ram"]):
                w("  { 0x%06x, %d, %s_ram%d }," % (addr, len(b), p, j))
            w("};")
            first, objs, rows = 0, [], []
            for c6, c7, cam, (o, at, variant) in seg["ticks"]:
                rows.append("  { %d, %d, 0x%08x, %d, %d, %d, %d }," % (
                    c6, c7, cam, len(o), at, variant, first))
                first += len(o); objs += o
            w("static const MarbleSprTick %s_ticks[] = {" % p)
            for r in rows: w(r)
            w("};")
            w("static const MarbleSprObj %s_objs[] = {" % p)
            for o in objs: w("  { %d, %d, %d, %d }," % o)
            w("};\n")
            seg["nobjs"] = len(objs)
        w("static const MarbleSprSegment marble_sprites_segments[] = {")
        for k, seg in enumerate(segs):
            p = "marble_spr_s%d" % k
            w("  { %d, %d, %d, %s_ram, %d, %s_ticks, %d, %s_objs }," % (seg["race"], seg["frame"],
              len(seg["ram"]), p, len(seg["ticks"]), p, seg["nobjs"], p))
        w("};")
        w("static const uint8_t MARBLE_SPRITES_SEGMENTS = %d;\n#endif" % len(segs))


# ---------------------------------------------------------------- deaths fixture

def death_run(t, trace, i, n, rng_in=False, full=None):
    """Death model (marble_sim.main_tick) from tick start snapshot i for n ticks, inputs
    only from the trace; every tick must equal the next snapshot. rng_in (tube fixture): the
    RNG word of each snapshot is an input too (silly race start: minis), all enemies run
    (marble_enemy.setup, as the C++ main tick). Returns (ticks, reads); a tick is (c6, c7,
    camera, frames[, rng], death_state). full (default rng_in): all enemies run."""
    import marble_enemy as me
    m = RecMem(t, trace[i][1])
    if rng_in if full is None else full:
        me.setup(m)
    else:
        m.deaths = m.objects = m.sprites = True
    ticks = []
    for (fr, r, v), (fr2, r2, v2) in zip(trace[i:i + n], trace[i + 1:i + n + 1]):
        inp = ms.death_inputs(r, r2)
        m.events = []; m.sounds = []
        rng = ms.tw(r, 0x4003a6) & 0xffff
        if rng_in: m.ww(0x4003a6, rng)
        ms.main_tick(m, *inp)
        d = [e for e in ms.death_diff(m, r2, v2) if not (rng_in and e.startswith("rng"))]
        if d: raise SystemExit("frame %d: differs from the trace: %s" % (fr2, "; ".join(d[:6])))
        ticks.append((ms.s8(inp[0]), ms.s8(inp[1])) + inp[2:] + ((rng,) if rng_in else ())
                     + (ms.death_state(m),))
    if len(ticks) != n: raise SystemExit("only %d ticks in the trace" % len(ticks))
    return ticks, m.first_read


def death_events(ticks):
    ev = {"ticks": len(ticks), "states": {}, "frames": set(), "shards": set(), "masks": 0,
          "max mos": 0}
    for *_, (x, y, z, st, vis, sx, sy, anim, hdr, shard, mask, rng, mos) in ticks:
        ev["states"][st] = ev["states"].get(st, 0) + 1
        if hdr: ev["frames"].add(hdr)
        if shard: ev["shards"].add(shard)
        ev["masks"] += mask[0] & 0x40 != 0
        ev["max mos"] = max(ev["max mos"], len(mos))
    ev["frames"], ev["shards"] = len(ev["frames"]), len(ev["shards"])
    return ev


DEATH_HEADER = ("Death", "marble_death", "marble_deaths", "MARBLE_DEATHS", "dwrite", "dfixture", (
    "// Marble player death host test: MAME tick start traces (tools/mame_trace.lua",
    "// MARBLE_TICK=start) between MARBLE_PLACE teleports: dizzy (state 1), shatter (2),",
    "// lost (4), respawn. marble_sim.py main_tick reproduces them from the initial work RAM",
    "// blocks and the inputs alone. Derived numbers only. Replay check:"))
TUBE_HEADER = ("Tube", "marble_tube", "marble_tubes", "MARBLE_TUBES", "twrite", "tfixture", (
    "// Marble tube host test: MAME tick start traces (tools/mame_trace.lua MARBLE_TICK=start,",
    "// MARBLE_PLACE next to a tube, stick input). Silly: funnel 0x22 lift and drop out of a",
    "// pipe exit (state 3), pipe exit walls 0x23/0x24 (shape 0x1cd00), lost, respawn. Beginner",
    "// 0x12 hole, 0x13/0x14 entries (one mouth wall bounce), walls 0x15/0x16; intermediate",
    "// 0x20 hole, walls 0x21; ultimate 0x25 hole, walls 0x26/0x27. marble_sim.py main_tick",
    "// reproduces them from the initial work RAM blocks and the inputs alone (the RNG word is",
    "// an input: the minis are not ported). Derived numbers only. Replay check:"))


AERIAL_HEADER = ("Aerial", "marble_aerial", "marble_aerials", "MARBLE_AERIAL", "awrite",
                 "afixture", (
    "// Marble aerial object host test: MAME tick start traces (tools/mame_trace.lua",
    "// MARBLE_TICK=start, MARBLE_PLACE on the aerial course, stick input): vacuum gate pull and",
    "// catch (state 0xa, stars, respawn), 0x0d gate frame bounce, catapult throw, bounce box",
    "// hits (dizzy). marble_sim.py main_tick with all enemies reproduces them from the initial",
    "// work RAM blocks and the inputs alone. Derived numbers only. Replay check:"))


def write_deaths(out, segs, hdr, rng_in=False):
    """Death style fixture header (MarbleDeath* / MarbleTube* names; rng_in: per tick RNG
    input word after frames)."""
    name, p0, arr, guard, cmd, check, intro = hdr
    with open(out, "w") as f:
        w = lambda s="": print(s, file=f)
        w("// Generated by romconv/internal/marble/tools/make_fixture.py %s; do not edit." % cmd)
        for line in intro: w(line)
        w("//   python3 romconv/internal/marble/tools/marble_sim.py %s <this file>" % check)
        w("#ifndef %s_TRACE_H\n#define %s_TRACE_H\n#include <stdint.h>\n" % (guard, guard))
        w("// Work RAM 0x400000-0x401fff is 0 except these blocks (68010 address, big-endian).")
        w("struct Marble%sRam { uint32_t addr; uint16_t len; const uint8_t *bytes; };" % name)
        w("// One player 1 MO of 0x26f3e (type 1) as in MO RAM: word 0 y << 5 | hflip 0x8000 |")
        w("// height - 1, word 1 code | palette set << 11, word 2 x << 5 | priority 0x8000.")
        w("struct Marble%sMo { uint16_t w0, code, w2; };" % name)
        w("// One main loop tick (marble_sim.py main_tick): inputs c6, c7 (trackball deltas of")
        w("// the tick, +0xc6/+0xc7), camera (long 0x40097c after the camera step), frames")
        if rng_in:
            w("// (0x400010 at the tick start), rng_in (0x4003a6 at the tick start). Then player")
            w("// 1: x, y, z (16.16), state (+0x1a), vis")
        else:
            w("// (0x400010 at the tick start). Then player 1: x, y, z (16.16), state (+0x1a), vis")
        w("// (+0x1c), sx, sy (+0x1e/+0x20, MO position sx + 0x18, sy + 0x10), anim (+0x5a),")
        w("// frame_hdr (frame header rl(anim) drawn in states 1, 2, 5, 9 with palette set 0,")
        w("// else 0), shard_hdr (shatter overlay rl(+0x62) when +0x67, set 2, else 0), mask")
        w("// (+0x38-+0x55: 5 entries of big endian code, x, y; mask codes have bit 14, the")
        w("// state 4 sparkle 0x13ade writes star entries without it), rng (0x4003a6), then")
        w("// the n player MOs mos[first .. first + n).")
        w("struct Marble%sTick {" % name)
        w("  int8_t c6, c7; uint32_t camera, frames;%s" % (" uint16_t rng_in;" if rng_in else ""))
        w("  int32_t x, y, z; uint8_t state, vis; int16_t sx, sy;")
        w("  uint32_t anim, frame_hdr, shard_hdr; uint8_t mask[30]; uint16_t rng;")
        w("  uint8_t n; uint16_t first;")
        w("};")
        w("struct Marble%sSegment {" % name)
        w("  uint8_t race; uint32_t frame;  // race 0-5, MAME frame of the first snapshot")
        w("  uint8_t nram; const Marble%sRam *ram; uint16_t nticks; const Marble%sTick *ticks;"
          % (name, name))
        w("  uint16_t nmos; const Marble%sMo *mos;" % name)
        w("};\n")
        for k, seg in enumerate(segs):
            p = "%s_s%d" % (p0, k)
            for j, (addr, b) in enumerate(seg["ram"]):
                w("static const uint8_t %s_ram%d[] = {\n%s\n};" % (p, j, c_bytes(b)))
            w("static const Marble%sRam %s_ram[] = {" % (name, p))
            for j, (addr, b) in enumerate(seg["ram"]):
                w("  { 0x%06x, %d, %s_ram%d }," % (addr, len(b), p, j))
            w("};")
            first, mos = 0, []
            w("static const Marble%sTick %s_ticks[] = {" % (name, p))
            for tick in seg["ticks"]:
                c6, c7, cam, fr = tick[:4]
                rin = (" 0x%04x," % tick[4]) if rng_in else ""
                x, y, z, st, vis, sx, sy, anim, hdr_, shard, mask, rng, mo = tick[-1]
                w("  { %d, %d, 0x%08x, %d,%s %s, %s, %s, %d, %d, %d, %d, 0x%x, 0x%x, 0x%x, {%s}, "
                  "0x%04x, %d, %d }," % (c6, c7, cam, fr, rin, c_int(x), c_int(y), c_int(z), st,
                                         vis, sx, sy, anim, hdr_, shard,
                                         ",".join(str(v) for v in mask), rng, len(mo), first))
                first += len(mo); mos += mo
            w("};")
            w("static const Marble%sMo %s_mos[] = {" % (name, p))
            for k2 in range(0, len(mos), 4):
                w("  " + " ".join("{0x%x,0x%x,0x%x}," % mo for mo in mos[k2:k2 + 4]))
            w("};\n")
            seg["nmos"] = len(mos)
        w("static const Marble%sSegment %s_segments[] = {" % (name, arr))
        for k, seg in enumerate(segs):
            p = "%s_s%d" % (p0, k)
            w("  { %d, %d, %d, %s_ram, %d, %s_ticks, %d, %s_mos }," % (
                seg["race"], seg["frame"], len(seg["ram"]), p, len(seg["ticks"]), p,
                seg["nmos"], p))
        w("};")
        w("static const uint8_t %s_SEGMENTS = %d;\n#endif" % (guard, len(segs)))


def death_segments(specs, rng_in=False, full=None):
    segs = []
    for spec in specs:
        race, path, frame, n = spec.split(":")
        race, frame, n = int(race), int(frame), int(n)
        trace = ms.load_trace(path)
        i = next(k for k, x in enumerate(trace) if x[0] == frame)
        t = ms.Terrain(race)
        ticks, reads = death_run(t, trace, i, n, rng_in, full)
        seg = {"race": race, "frame": frame, "ram": obj_blocks(trace[i][1], reads),
               "ticks": ticks}
        bad = ms.replay_dfixture(seg, ms.fixture_mem(seg, t), rng_in, full)
        if bad is not None:
            raise SystemExit("race %d frame %d: does not replay from its RAM blocks (tick %d)"
                             % (race, frame, bad))
        print("race %d frame %d: %s, RAM %s" % (race, frame, death_events(ticks),
              ", ".join("%06x+%d" % (a, len(b)) for a, b in seg["ram"])))
        segs.append(seg)
    return segs


BONUS_HEADER = ("Bonus", "marble_bonus", "marble_bonuses", "MARBLE_BONUS", "tbwrite",
                "tbfixture", (
    "// Marble time bonus host test: MAME tick start traces (tools/mame_trace.lua",
    "// MARBLE_TICK=start, MARBLE_PLACE into a zone of 0x24aae, MARBLE_POKE 5 s left and the",
    "// event armed): 0x1bbaa sets +0xcb, state 0 enters state 8 (+1 s every 9 ticks, 10",
    "// times), then rolling. marble_sim.py main_tick with all enemies reproduces them from the",
    "// initial work RAM blocks and the inputs alone. Derived numbers only. Replay check:"))


def cmd_tbwrite(out, specs):
    write_deaths(out, death_segments(specs, False, True), BONUS_HEADER)


def cmd_dwrite(out, specs):
    write_deaths(out, death_segments(specs), DEATH_HEADER)


def cmd_twrite(out, specs):
    """A spec ending in .h keeps the segments of that tube fixture (traces gone from $TMPDIR)."""
    segs = []
    for spec in specs:
        segs += (ms.load_dfixture(spec, True) if spec.endswith(".h")
                 else death_segments([spec], True))
    write_deaths(out, segs, TUBE_HEADER, True)


def cmd_awrite(out, specs):
    write_deaths(out, death_segments(specs, False, True), AERIAL_HEADER)


# ---------------------------------------------------------------- steelies fixture

def steelie_run(t, trace, i, n, drops):
    """Steelie model (main_tick + 0x26f3e sort) from tick start snapshot i, the MARBLE_DROP
    writes applied to it first, for n ticks; inputs only from the trace; every tick must equal
    the next snapshot. Returns (ticks, reads, start RAM)."""
    import marble_enemy as me
    start = me.steelie_mem(t, trace[i][1])
    for d in drops: me.drop(start, *d)
    m = me.setup(RecMem(t, bytes(start.ram)))
    ticks = []
    for (fr, r, v), (fr2, r2, v2) in zip(trace[i:i + n], trace[i + 1:i + n + 1]):
        inp = ms.death_inputs(r, r2)
        m.events = []; m.sounds = []
        ms.main_tick(m, *inp)
        mo.boxes_and_sort(m)
        d = me.steelie_diff(m, r2, v2)
        if d: raise SystemExit("frame %d: differs from the trace: %s" % (fr2, "; ".join(d[:6])))
        ticks.append((ms.s8(inp[0]), ms.s8(inp[1])) + inp[2:] + (me.tick_state(m),))
    if len(ticks) != n: raise SystemExit("only %d ticks in the trace" % len(ticks))
    return ticks, m.first_read, bytes(start.ram)


def steelie_events(ticks):
    ev = {"ticks": len(ticks), "player": {}, "modes": {}, "shatter": 0, "masks": 0, "list max": 0}
    for *_, (pl, s0, s1, lst) in ticks:
        ev["player"][pl[3]] = ev["player"].get(pl[3], 0) + 1
        for s in (s0, s1):
            if not s[0]: continue
            ev["modes"][hex(s[1])] = ev["modes"].get(hex(s[1]), 0) + 1
            ev["shatter"] += s[0] == 2
            ev["masks"] += s[13][0] & 0x40 != 0
        ev["list max"] = max(ev["list max"], len(lst))
    return ev


def cmd_stwrite(out, specs):
    import marble_enemy as me
    segs = []
    for spec in specs:
        f = spec.split(":")
        race, path, frame, n = int(f[0]), f[1], int(f[2]), int(f[3])
        drops = [(int(f[4], 16), int(f[5]), int(f[6]), int(f[7]))] if len(f) > 4 else []
        trace = ms.load_trace(path)
        i = next(k for k, x in enumerate(trace) if x[0] == frame)
        t = ms.Terrain(race)
        ticks, reads, ram = steelie_run(t, trace, i, n, drops)
        seg = {"race": race, "frame": frame, "ram": obj_blocks(ram, reads), "ticks": ticks}
        bad = me.replay_sfixture(seg, ms.fixture_mem(seg, t))
        if bad is not None:
            raise SystemExit("race %d frame %d: does not replay from its RAM blocks (tick %d)"
                             % (race, frame, bad))
        print("race %d frame %d: %s, RAM %s" % (race, frame, steelie_events(ticks),
              ", ".join("%06x+%d" % (a, len(b)) for a, b in seg["ram"])))
        segs.append(seg)
    with open(out, "w") as f:
        w = lambda s="": print(s, file=f)
        w("// Generated by romconv/internal/marble/tools/make_fixture.py stwrite; do not edit.")
        w("// Marble steelie host test: MAME tick start traces (tools/mame_trace.lua")
        w("// MARBLE_TICK=start, MARBLE_DROP drops a steelie or the player): spawn by camera band,")
        w("// patrol/chase/hunt/return/stun, pair collisions, shatter, removal, a steelie breaking")
        w("// the player. marble_enemy.py main_tick + 0x26f3e sort reproduces them from the")
        w("// initial work RAM blocks and the inputs alone. Derived numbers only. Replay check:")
        w("//   python3 romconv/internal/marble/tools/marble_enemy.py sfixture <this file>")
        w("#ifndef MARBLE_STEELIES_TRACE_H\n#define MARBLE_STEELIES_TRACE_H\n#include <stdint.h>\n")
        w("// Work RAM 0x400000-0x401fff is 0 except these blocks (68010 address, big-endian).")
        w("struct MarbleSteelieRam { uint32_t addr; uint16_t len; const uint8_t *bytes; };")
        w("// One MO of 0x26f3e as in MO RAM: word 0 y << 5 | hflip 0x8000 | height - 1, word 1")
        w("// code | palette set << 11, word 2 x << 5 | priority 0x8000.")
        w("struct MarbleSteelieMo { uint16_t w0, code, w2; };")
        w("// Draw list entry in link order (0x4003bc): type, index.")
        w("struct MarbleSteelieEntry { uint8_t type, index; };")
        w("// Steelie i (0x4009a4 + 0x7c i) after the tick: live (+0x18: 1, 2 shattering), mode")
        w("// (+0x1a: 0x20 patrol, 0x21 chase, 0x22 hunt, 0x23 return, 0x24 stunned, 2 shatter), vis")
        w("// (+0x1c), x, y, z (16.16), sx, sy (+0x1e/+0x20), anim (+0x5a), frame_hdr (frame header")
        w("// drawn: 0x21f36 set 5, rl(anim) while shattering, 0 if not in the list or not visible),")
        w("// shard_hdr (rl(+0x62) when +0x67, set 2), anger (+0x6c), node (+0x6e), mask (+0x38:")
        w("// 5 entries of big endian code, x, y), its n MOs mos[first ..].")
        w("struct MarbleSteelieObj {")
        w("  uint8_t live, mode, vis; int32_t x, y, z; int16_t sx, sy;")
        w("  uint32_t anim, frame_hdr, shard_hdr; uint16_t anger; uint32_t node; uint8_t mask[30];")
        w("  uint8_t n; uint16_t first;")
        w("};")
        w("// One main loop tick: inputs c6, c7 (trackball deltas), camera (long 0x40097c after the")
        w("// camera step), frames (0x400010 at the tick start). Player 1 as MarbleDeathTick: x, y,")
        w("// z, state, vis, sx, sy, anim, frame_hdr, shard_hdr, mask, rng (0x4003a6), its MOs. Both")
        w("// steelies, then the draw list list[lfirst .. lfirst + nlist).")
        w("struct MarbleSteelieTick {")
        w("  int8_t c6, c7; uint32_t camera, frames;")
        w("  int32_t x, y, z; uint8_t state, vis; int16_t sx, sy;")
        w("  uint32_t anim, frame_hdr, shard_hdr; uint8_t mask[30]; uint16_t rng;")
        w("  uint8_t n; uint16_t first;")
        w("  MarbleSteelieObj s[2];")
        w("  uint8_t nlist; uint16_t lfirst;")
        w("};")
        w("struct MarbleSteelieSegment {")
        w("  uint8_t race; uint32_t frame;  // race 0-5, MAME frame of the first snapshot")
        w("  uint8_t nram; const MarbleSteelieRam *ram; uint16_t nticks; const MarbleSteelieTick *ticks;")
        w("  uint16_t nmos; const MarbleSteelieMo *mos; uint16_t nlist; const MarbleSteelieEntry *list;")
        w("};\n")
        for k, seg in enumerate(segs):
            p = "marble_steelie_s%d" % k
            for j, (addr, b) in enumerate(seg["ram"]):
                w("static const uint8_t %s_ram%d[] = {\n%s\n};" % (p, j, c_bytes(b)))
            w("static const MarbleSteelieRam %s_ram[] = {" % p)
            for j, (addr, b) in enumerate(seg["ram"]):
                w("  { 0x%06x, %d, %s_ram%d }," % (addr, len(b), p, j))
            w("};")
            mos, lst = [], []

            def add(ms_):
                mos.extend(ms_); return "%d, %d" % (len(ms_), len(mos) - len(ms_))

            def obj(s):
                live, mode, vis, x, y, z, sx, sy, anim, hdr, shard, anger, node, mask, smo = s
                return ("{ %d, 0x%x, %d, %s, %s, %s, %d, %d, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, {%s}, %s }"
                        % (live, mode, vis, c_int(x), c_int(y), c_int(z), sx, sy, anim, hdr, shard,
                           anger, node, ",".join(str(v) for v in mask), add(smo)))
            w("static const MarbleSteelieTick %s_ticks[] = {" % p)
            for c6, c7, cam, fr, (pl, s0, s1, dl) in seg["ticks"]:
                x, y, z, st, vis, sx, sy, anim, hdr, shard, mask, rng, pmo = pl
                head = ("{ %d, %d, 0x%08x, %d, %s, %s, %s, %d, %d, %d, %d, 0x%x, 0x%x, 0x%x, {%s}, "
                        "0x%04x, %s," % (c6, c7, cam, fr, c_int(x), c_int(y), c_int(z), st, vis, sx,
                                         sy, anim, hdr, shard, ",".join(str(v) for v in mask), rng,
                                         add(pmo)))
                w("  " + head)
                w("    { %s,\n      %s }, %d, %d }," % (obj(s0), obj(s1), len(dl), len(lst)))
                lst.extend(dl)
            w("};")
            w("static const MarbleSteelieMo %s_mos[] = {" % p)
            for k2 in range(0, len(mos), 4):
                w("  " + " ".join("{0x%x,0x%x,0x%x}," % m_ for m_ in mos[k2:k2 + 4]))
            w("};")
            w("static const MarbleSteelieEntry %s_list[] = {" % p)
            for k2 in range(0, len(lst), 8):
                w("  " + " ".join("{%d,%d}," % e for e in lst[k2:k2 + 8]))
            w("};\n")
            seg["nmos"], seg["nlist"] = len(mos), len(lst)
        w("static const MarbleSteelieSegment marble_steelies_segments[] = {")
        for k, seg in enumerate(segs):
            p = "marble_steelie_s%d" % k
            w("  { %d, %d, %d, %s_ram, %d, %s_ticks, %d, %s_mos, %d, %s_list }," % (
                seg["race"], seg["frame"], len(seg["ram"]), p, len(seg["ticks"]), p,
                seg["nmos"], p, seg["nlist"], p))
        w("};")
        w("static const uint8_t MARBLE_STEELIES_SEGMENTS = %d;\n#endif" % len(segs))


def slinky_run(t, trace, i, n):
    """Slinky model (marble_slinky.setup: steelies and slinkies) from tick start snapshot i
    for n ticks; every tick must equal the next snapshot. Returns (ticks, reads, start RAM)."""
    import marble_slinky as sl
    m = sl.setup(RecMem(t, bytes(trace[i][1])))
    ticks = []
    for (fr, r, v), (fr2, r2, v2) in zip(trace[i:i + n], trace[i + 1:i + n + 1]):
        inp = ms.death_inputs(r, r2)
        m.events = []; m.sounds = []
        ms.main_tick(m, *inp)
        mo.boxes_and_sort(m)
        d = sl.slinky_diff(m, r2, v2)
        if d: raise SystemExit("frame %d: differs from the trace: %s" % (fr2, "; ".join(d[:6])))
        ticks.append((ms.s8(inp[0]), ms.s8(inp[1])) + inp[2:] + (sl.tick_state(m),))
    if len(ticks) != n: raise SystemExit("only %d ticks in the trace" % len(ticks))
    return ticks, m.first_read, bytes(trace[i][1])


def slinky_events(ticks):
    ev = {"ticks": len(ticks), "player": {}, "states": {}, "masks": 0, "list max": 0}
    for *_, st in ticks:
        pl, lst = st[0], st[-1]
        ev["player"][pl[3]] = ev["player"].get(pl[3], 0) + 1
        for s in st[3:7]:
            if not s[0]: continue
            ev["states"][s[1]] = ev["states"].get(s[1], 0) + 1
            ev["masks"] += any(s[12])
        ev["list max"] = max(ev["list max"], len(lst))
    return ev


def cmd_slwrite(out, specs):
    import marble_slinky as sl
    segs = []
    for spec in specs:
        f = spec.split(":")
        race, path, frame, n = int(f[0]), f[1], int(f[2]), int(f[3])
        trace = ms.load_trace(path)
        i = next(k for k, x in enumerate(trace) if x[0] == frame)
        t = ms.Terrain(race)
        ticks, reads, ram = slinky_run(t, trace, i, n)
        seg = {"race": race, "frame": frame, "ram": obj_blocks(ram, reads), "ticks": ticks}
        bad = sl.replay_slfixture(seg, ms.fixture_mem(seg, t))
        if bad is not None:
            raise SystemExit("race %d frame %d: does not replay from its RAM blocks (tick %d)"
                             % (race, frame, bad))
        print("race %d frame %d: %s, RAM %s" % (race, frame, slinky_events(ticks),
              ", ".join("%06x+%d" % (a, len(b)) for a, b in seg["ram"])))
        segs.append(seg)
    with open(out, "w") as f:
        w = lambda s="": print(s, file=f)
        w("// Generated by romconv/internal/marble/tools/make_fixture.py slwrite; do not edit.")
        w("// Marble slinky host test: MAME tick start traces (tools/mame_trace.lua")
        w("// MARBLE_TICK=start, MARBLE_PLACE puts the marble on a slinky path): spawn by camera")
        w("// band, waypoint walk, push, chase, lunge, catch, lost prey, the player eaten (states")
        w("// 5, 7, then lost), steelies alongside. marble_slinky.py main_tick + 0x26f3e sort")
        w("// reproduces them from the initial work RAM blocks and the inputs alone, acid")
        w("// alongside. Derived numbers only. Replay check:")
        w("//   python3 romconv/internal/marble/tools/marble_slinky.py slfixture <this file>")
        w("#ifndef MARBLE_SLINKIES_TRACE_H\n#define MARBLE_SLINKIES_TRACE_H\n#include <stdint.h>\n")
        w("// Work RAM 0x400000-0x401fff is 0 except these blocks (68010 address, big-endian).")
        w("struct MarbleSlinkyRam { uint32_t addr; uint16_t len; const uint8_t *bytes; };")
        w("// One MO of 0x26f3e as in MO RAM: word 0 y << 5 | hflip 0x8000 | height - 1, word 1")
        w("// code | palette set << 11, word 2 x << 5 | priority 0x8000.")
        w("struct MarbleSlinkyMo { uint16_t w0, code, w2; };")
        w("// Draw list entry in link order (0x4003bc): type, index.")
        w("struct MarbleSlinkyEntry { uint8_t type, index; };")
        w("// Steelie as MarbleSteelieObj (test_marble_steelies/marble_steelies_trace.h).")
        w("struct MarbleSlinkySteelie {")
        w("  uint8_t live, mode, vis; int32_t x, y, z; int16_t sx, sy;")
        w("  uint32_t anim, frame_hdr, shard_hdr; uint16_t anger; uint32_t node; uint8_t mask[30];")
        w("  uint8_t n; uint16_t first;")
        w("};")
        w("// Slinky i (0x401302 + 0x60 i) after the tick: live (+0x18), state (+0x1a: 0 walk, 1")
        w("// chase, 2 pushed, 3 back, 4 stopped, 5 ate, 6 lost the prey), x, y, z (16.16), sx, sy")
        w("// (+0x28/+0x2a), tick (+0x24-+0x27 big endian), next (+0x4a), prey (+0x56), cursor")
        w("// (+0x58), frame_hdr (rl(cursor), 0 if not in the draw list), mask (+0x2c: 5 entries of")
        w("// big endian code, x, y), its n MOs mos[first ..].")
        w("struct MarbleSlinkyObj {")
        w("  uint8_t live, state; int32_t x, y, z; int16_t sx, sy;")
        w("  uint32_t tick, next; uint16_t prey; uint32_t cursor, frame_hdr; uint8_t mask[30];")
        w("  uint8_t n; uint16_t first;")
        w("};")
        w("// One main loop tick: inputs c6, c7, camera (long 0x40097c after the camera step), frames")
        w("// (0x400010 at the tick start). Player 1 as MarbleDeathTick, both steelies, the four")
        w("// slinkies, then the draw list list[lfirst .. lfirst + nlist).")
        w("struct MarbleSlinkyTick {")
        w("  int8_t c6, c7; uint32_t camera, frames;")
        w("  int32_t x, y, z; uint8_t state, vis; int16_t sx, sy;")
        w("  uint32_t anim, frame_hdr, shard_hdr; uint8_t mask[30]; uint16_t rng;")
        w("  uint8_t n; uint16_t first;")
        w("  MarbleSlinkySteelie s[2];")
        w("  MarbleSlinkyObj l[4];")
        w("  uint8_t nlist; uint16_t lfirst;")
        w("};")
        w("struct MarbleSlinkySegment {")
        w("  uint8_t race; uint32_t frame;  // race 0-5, MAME frame of the first snapshot")
        w("  uint8_t nram; const MarbleSlinkyRam *ram; uint16_t nticks; const MarbleSlinkyTick *ticks;")
        w("  uint16_t nmos; const MarbleSlinkyMo *mos; uint16_t nlist; const MarbleSlinkyEntry *list;")
        w("};\n")
        for k, seg in enumerate(segs):
            p = "marble_slinky_s%d" % k
            for j, (addr, b) in enumerate(seg["ram"]):
                w("static const uint8_t %s_ram%d[] = {\n%s\n};" % (p, j, c_bytes(b)))
            w("static const MarbleSlinkyRam %s_ram[] = {" % p)
            for j, (addr, b) in enumerate(seg["ram"]):
                w("  { 0x%06x, %d, %s_ram%d }," % (addr, len(b), p, j))
            w("};")
            mos, lst = [], []

            def add(ms_):
                mos.extend(ms_); return "%d, %d" % (len(ms_), len(mos) - len(ms_))

            def steelie(s):
                live, mode, vis, x, y, z, sx, sy, anim, hdr, shard, anger, node, mask, smo = s
                return ("{ %d, 0x%x, %d, %s, %s, %s, %d, %d, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, {%s}, %s }"
                        % (live, mode, vis, c_int(x), c_int(y), c_int(z), sx, sy, anim, hdr, shard,
                           anger, node, ",".join(str(v) for v in mask), add(smo)))

            def slinky(s):
                live, st, x, y, z, sx, sy, tick, nxt, prey, cur, hdr, mask, smo = s
                return ("{ %d, %d, %s, %s, %s, %d, %d, 0x%x, 0x%x, %d, 0x%x, 0x%x, {%s}, %s }"
                        % (live, st, c_int(x), c_int(y), c_int(z), sx, sy, tick, nxt, prey, cur,
                           hdr, ",".join(str(v) for v in mask), add(smo)))
            w("static const MarbleSlinkyTick %s_ticks[] = {" % p)
            for c6, c7, cam, fr, st in seg["ticks"]:
                pl, s0, s1, dl = st[0], st[1], st[2], st[-1]
                x, y, z, pst, vis, sx, sy, anim, hdr, shard, mask, rng, pmo = pl
                w("  { %d, %d, 0x%08x, %d, %s, %s, %s, %d, %d, %d, %d, 0x%x, 0x%x, 0x%x, {%s}, "
                  "0x%04x, %s," % (c6, c7, cam, fr, c_int(x), c_int(y), c_int(z), pst, vis, sx, sy,
                                   anim, hdr, shard, ",".join(str(v) for v in mask), rng, add(pmo)))
                w("    { %s,\n      %s }," % (steelie(s0), steelie(s1)))
                w("    { %s }, %d, %d }," % (",\n      ".join(slinky(o) for o in st[3:7]),
                                            len(dl), len(lst)))
                lst.extend(dl)
            w("};")
            w("static const MarbleSlinkyMo %s_mos[] = {" % p)
            for k2 in range(0, len(mos), 4):
                w("  " + " ".join("{0x%x,0x%x,0x%x}," % m_ for m_ in mos[k2:k2 + 4]))
            w("};")
            w("static const MarbleSlinkyEntry %s_list[] = {" % p)
            for k2 in range(0, len(lst), 8):
                w("  " + " ".join("{%d,%d}," % e for e in lst[k2:k2 + 8]))
            w("};\n")
            seg["nmos"], seg["nlist"] = len(mos), len(lst)
        w("static const MarbleSlinkySegment marble_slinkies_segments[] = {")
        for k, seg in enumerate(segs):
            p = "marble_slinky_s%d" % k
            w("  { %d, %d, %d, %s_ram, %d, %s_ticks, %d, %s_mos, %d, %s_list }," % (
                seg["race"], seg["frame"], len(seg["ram"]), p, len(seg["ticks"]), p,
                seg["nmos"], p, seg["nlist"], p))
        w("};")
        w("static const uint8_t MARBLE_SLINKIES_SEGMENTS = %d;\n#endif" % len(segs))


def acid_run(t, trace, i, n):
    """Acid model (marble_enemy.setup: steelies, slinkies, acid) from tick start snapshot i
    for n ticks; every tick must equal the next snapshot. Returns (ticks, reads, start RAM)."""
    import marble_enemy as me, marble_acid as ac
    m = me.setup(RecMem(t, bytes(trace[i][1])))
    ticks = []
    for (fr, r, v), (fr2, r2, v2) in zip(trace[i:i + n], trace[i + 1:i + n + 1]):
        inp = ms.death_inputs(r, r2)
        m.events = []; m.sounds = []
        ms.main_tick(m, *inp)
        mo.boxes_and_sort(m)
        d = ac.acid_diff(m, r2, v2)
        if d: raise SystemExit("frame %d: differs from the trace: %s" % (fr2, "; ".join(d[:6])))
        ticks.append((ms.s8(inp[0]), ms.s8(inp[1])) + inp[2:] + (ac.tick_state(m),))
    if len(ticks) != n: raise SystemExit("only %d ticks in the trace" % len(ticks))
    return ticks, m.first_read, bytes(trace[i][1])


def acid_events(ticks):
    ev = {"ticks": len(ticks), "player": {}, "live": 0, "drawn": 0, "list max": 0}
    for *_, st in ticks:
        pl, lst = st[0], st[-1]
        ev["player"][pl[3]] = ev["player"].get(pl[3], 0) + 1
        ev["live"] = max(ev["live"], sum(1 for o in st[1:8] if o[0]))
        ev["drawn"] += sum(1 for o in st[1:8] if o[10])
        ev["list max"] = max(ev["list max"], len(lst))
    return ev


def cmd_acwrite(out, specs):
    import marble_acid as ac
    segs = []
    for spec in specs:
        f = spec.split(":")
        race, path, frame, n = int(f[0]), f[1], int(f[2]), int(f[3])
        trace = ms.load_trace(path)
        i = next(k for k, x in enumerate(trace) if x[0] == frame)
        t = ms.Terrain(race)
        ticks, reads, ram = acid_run(t, trace, i, n)
        seg = {"race": race, "frame": frame, "ram": obj_blocks(ram, reads), "ticks": ticks}
        bad = ac.replay_acfixture(seg, ms.fixture_mem(seg, t))
        if bad is not None:
            raise SystemExit("race %d frame %d: does not replay from its RAM blocks (tick %d)"
                             % (race, frame, bad))
        print("race %d frame %d: %s, RAM %s" % (race, frame, acid_events(ticks),
              ", ".join("%06x+%d" % (a, len(b)) for a, b in seg["ram"])))
        segs.append(seg)
    with open(out, "w") as f:
        w = lambda s="": print(s, file=f)
        w("// Generated by romconv/internal/marble/tools/make_fixture.py acwrite; do not edit.")
        w("// Marble acid host test: MAME tick start traces (tools/mame_trace.lua MARBLE_TICK=start,")
        w("// MARBLE_PLACE puts the marble on an acid): spawn by camera band (random group), path")
        w("// crawl, the marble dissolved (state 9, then lost), slinkies and steelies alongside.")
        w("// marble_acid.py main_tick + 0x26f3e sort reproduces them from the initial work RAM")
        w("// blocks and the inputs alone. Derived numbers only. Replay check:")
        w("//   python3 romconv/internal/marble/tools/marble_acid.py acfixture <this file>")
        w("#ifndef MARBLE_ACID_TRACE_H\n#define MARBLE_ACID_TRACE_H\n#include <stdint.h>\n")
        w("// Work RAM 0x400000-0x401fff is 0 except these blocks (68010 address, big-endian).")
        w("struct MarbleAcidRam { uint32_t addr; uint16_t len; const uint8_t *bytes; };")
        w("// One MO of 0x26f3e as in MO RAM: word 0 y << 5 | hflip 0x8000 | height - 1, word 1")
        w("// code | palette set << 11, word 2 x << 5 | priority 0x8000.")
        w("struct MarbleAcidMo { uint16_t w0, code, w2; };")
        w("// Draw list entry in link order (0x4003bc): type, index.")
        w("struct MarbleAcidEntry { uint8_t type, index; };")
        w("// Acid i (0x401482 + 0x42 i) after the tick: live (+0x18), x, y, z (16.16), sx, sy")
        w("// (+0x28/+0x2a), tick (+0x24/+0x25 big endian), path (+0x2c cursor), cursor (+0x3a")
        w("// frame cursor), frame_hdr (rl(cursor), 0 if not in the draw list), its n MOs")
        w("// mos[first ..].")
        w("struct MarbleAcidObj {")
        w("  uint8_t live; int32_t x, y, z; int16_t sx, sy;")
        w("  uint16_t tick; uint32_t path, cursor, frame_hdr;")
        w("  uint8_t n; uint16_t first;")
        w("};")
        w("// One main loop tick: inputs c6, c7, camera (long 0x40097c after the camera step), frames")
        w("// (0x400010 at the tick start). Player 1 as MarbleDeathTick, the seven acids, then the")
        w("// draw list list[lfirst .. lfirst + nlist).")
        w("struct MarbleAcidTick {")
        w("  int8_t c6, c7; uint32_t camera, frames;")
        w("  int32_t x, y, z; uint8_t state, vis; int16_t sx, sy;")
        w("  uint32_t anim, frame_hdr, shard_hdr; uint8_t mask[30]; uint16_t rng;")
        w("  uint8_t n; uint16_t first;")
        w("  MarbleAcidObj a[7];")
        w("  uint8_t nlist; uint16_t lfirst;")
        w("};")
        w("struct MarbleAcidSegment {")
        w("  uint8_t race; uint32_t frame;  // race 0-5, MAME frame of the first snapshot")
        w("  uint8_t nram; const MarbleAcidRam *ram; uint16_t nticks; const MarbleAcidTick *ticks;")
        w("  uint16_t nmos; const MarbleAcidMo *mos; uint16_t nlist; const MarbleAcidEntry *list;")
        w("};\n")
        for k, seg in enumerate(segs):
            p = "marble_acid_s%d" % k
            for j, (addr, b) in enumerate(seg["ram"]):
                w("static const uint8_t %s_ram%d[] = {\n%s\n};" % (p, j, c_bytes(b)))
            w("static const MarbleAcidRam %s_ram[] = {" % p)
            for j, (addr, b) in enumerate(seg["ram"]):
                w("  { 0x%06x, %d, %s_ram%d }," % (addr, len(b), p, j))
            w("};")
            mos, lst = [], []

            def add(ms_):
                mos.extend(ms_); return "%d, %d" % (len(ms_), len(mos) - len(ms_))

            def acid(o):
                live, x, y, z, sx, sy, tick, path_, cur, hdr, amo = o
                return ("{ %d, %s, %s, %s, %d, %d, 0x%x, 0x%x, 0x%x, 0x%x, %s }"
                        % (live, c_int(x), c_int(y), c_int(z), sx, sy, tick, path_, cur, hdr,
                           add(amo)))
            w("static const MarbleAcidTick %s_ticks[] = {" % p)
            for c6, c7, cam, fr, st in seg["ticks"]:
                pl, dl = st[0], st[-1]
                x, y, z, pst, vis, sx, sy, anim, hdr, shard, mask, rng, pmo = pl
                w("  { %d, %d, 0x%08x, %d, %s, %s, %s, %d, %d, %d, %d, 0x%x, 0x%x, 0x%x, {%s}, "
                  "0x%04x, %s," % (c6, c7, cam, fr, c_int(x), c_int(y), c_int(z), pst, vis, sx, sy,
                                   anim, hdr, shard, ",".join(str(v) for v in mask), rng, add(pmo)))
                w("    { %s }, %d, %d }," % (",\n      ".join(acid(o) for o in st[1:8]),
                                            len(dl), len(lst)))
                lst.extend(dl)
            w("};")
            w("static const MarbleAcidMo %s_mos[] = {" % p)
            for k2 in range(0, len(mos), 4):
                w("  " + " ".join("{0x%x,0x%x,0x%x}," % m_ for m_ in mos[k2:k2 + 4]))
            w("};")
            w("static const MarbleAcidEntry %s_list[] = {" % p)
            for k2 in range(0, len(lst), 8):
                w("  " + " ".join("{%d,%d}," % e for e in lst[k2:k2 + 8]))
            w("};\n")
            seg["nmos"], seg["nlist"] = len(mos), len(lst)
        w("static const MarbleAcidSegment marble_acid_segments[] = {")
        for k, seg in enumerate(segs):
            p = "marble_acid_s%d" % k
            w("  { %d, %d, %d, %s_ram, %d, %s_ticks, %d, %s_mos, %d, %s_list }," % (
                seg["race"], seg["frame"], len(seg["ram"]), p, len(seg["ticks"]), p,
                seg["nmos"], p, seg["nlist"], p))
        w("};")
        w("static const uint8_t MARBLE_ACID_SEGMENTS = %d;\n#endif" % len(segs))


def bird_run(t, trace, i, n):
    """Bird model (marble_enemy.setup: all modelled enemies) from tick start snapshot i for n
    ticks; every tick must equal the next snapshot. Returns (ticks, reads, start RAM)."""
    import marble_enemy as me, marble_bird as bd
    m = me.setup(RecMem(t, bytes(trace[i][1])))
    ticks = []
    for (fr, r, v), (fr2, r2, v2) in zip(trace[i:i + n], trace[i + 1:i + n + 1]):
        inp = ms.death_inputs(r, r2)
        m.events = []; m.sounds = []
        ms.main_tick(m, *inp)
        mo.boxes_and_sort(m)
        d = bd.bird_diff(m, r2, v2)
        if d: raise SystemExit("frame %d: differs from the trace: %s" % (fr2, "; ".join(d[:6])))
        ticks.append((ms.s8(inp[0]), ms.s8(inp[1])) + inp[2:] + (bd.tick_state(m),))
    if len(ticks) != n: raise SystemExit("only %d ticks in the trace" % len(ticks))
    return ticks, m.first_read, bytes(trace[i][1])


def bird_events(ticks):
    ev = {"ticks": len(ticks), "player": {}, "live": 0, "drawn": 0, "masked": 0, "list max": 0}
    for *_, st in ticks:
        pl, lst = st[0], st[-1]
        ev["player"][pl[3]] = ev["player"].get(pl[3], 0) + 1
        ev["live"] = max(ev["live"], sum(1 for o in st[1:11] if o[0]))
        ev["drawn"] += sum(1 for o in st[1:11] if o[10])
        ev["masked"] += sum(1 for o in st[1:11] if any(o[11]))
        ev["list max"] = max(ev["list max"], len(lst))
    return ev


def cmd_bdwrite(out, specs):
    import marble_bird as bd
    segs = []
    for spec in specs:
        f = spec.split(":")
        race, path, frame, n = int(f[0]), f[1], int(f[2]), int(f[3])
        trace = ms.load_trace(path)
        i = next(k for k, x in enumerate(trace) if x[0] == frame)
        t = ms.Terrain(race)
        ticks, reads, ram = bird_run(t, trace, i, n)
        seg = {"race": race, "frame": frame, "ram": obj_blocks(ram, reads), "ticks": ticks}
        bad = bd.replay_bdfixture(seg, ms.fixture_mem(seg, t))
        if bad is not None:
            raise SystemExit("race %d frame %d: does not replay from its RAM blocks (tick %d)"
                             % (race, frame, bad))
        print("race %d frame %d: %s, RAM %s" % (race, frame, bird_events(ticks),
              ", ".join("%06x+%d" % (a, len(b)) for a, b in seg["ram"])))
        segs.append(seg)
    with open(out, "w") as f:
        w = lambda s="": print(s, file=f)
        w("// Generated by romconv/internal/marble/tools/make_fixture.py bdwrite; do not edit.")
        w("// Marble silly race birds host test: MAME tick start traces (tools/mame_trace.lua")
        w("// MARBLE_TICK=start, MARBLE_PLACE puts the marble on a bird path): spawn while the")
        w("// camera band is in 3-0x1b, walk, landing, terrain masks (0x264aa mode 3), a bird")
        w("// carries the marble (state 0xb, then lost and respawn). marble_bird.py main_tick +")
        w("// 0x26f3e sort reproduces them from the initial work RAM blocks and the inputs alone.")
        w("// Derived numbers only. Replay check:")
        w("//   python3 romconv/internal/marble/tools/marble_bird.py bdfixture <this file>")
        w("#ifndef MARBLE_BIRDS_TRACE_H\n#define MARBLE_BIRDS_TRACE_H\n#include <stdint.h>\n")
        w("// Work RAM 0x400000-0x401fff is 0 except these blocks (68010 address, big-endian).")
        w("struct MarbleBirdRam { uint32_t addr; uint16_t len; const uint8_t *bytes; };")
        w("// One MO of 0x26f3e as in MO RAM: word 0 y << 5 | hflip 0x8000 | height - 1, word 1")
        w("// code | palette set << 11, word 2 x << 5 | priority 0x8000.")
        w("struct MarbleBirdMo { uint16_t w0, code, w2; };")
        w("// Draw list entry in link order (0x4003bc): type, index.")
        w("struct MarbleBirdEntry { uint8_t type, index; };")
        w("// Bird i (0x4019f8 + 0x38 i) after the tick: live (+0x18), state (+0x1a), x, y, z")
        w("// (16.16), sx, sy (+0x20/+0x22), tick (+0x24/+0x25 big endian), loops (+0x1b), cursor")
        w("// (+0x1c anim cursor), frame_hdr (rl(cursor), 0 if not in the draw list), mask (+0x26,")
        w("// 3 x {code, x, y} big endian), its n MOs mos[first ..].")
        w("struct MarbleBirdObj {")
        w("  uint8_t live, state; int32_t x, y, z; int16_t sx, sy;")
        w("  uint16_t tick; uint8_t loops; uint32_t cursor, frame_hdr; uint8_t mask[18];")
        w("  uint8_t n; uint16_t first;")
        w("};")
        w("// One main loop tick: inputs c6, c7, camera (long 0x40097c after the camera step), frames")
        w("// (0x400010 at the tick start). Player 1 as MarbleDeathTick, the ten birds, 0x400762")
        w("// (bird spawning on), then the draw list list[lfirst .. lfirst + nlist).")
        w("struct MarbleBirdTick {")
        w("  int8_t c6, c7; uint32_t camera, frames;")
        w("  int32_t x, y, z; uint8_t state, vis; int16_t sx, sy;")
        w("  uint32_t anim, frame_hdr, shard_hdr; uint8_t mask[30]; uint16_t rng;")
        w("  uint8_t n; uint16_t first;")
        w("  MarbleBirdObj b[10];")
        w("  uint8_t spawn_on; uint8_t nlist; uint16_t lfirst;")
        w("};")
        w("struct MarbleBirdSegment {")
        w("  uint8_t race; uint32_t frame;  // race 0-5, MAME frame of the first snapshot")
        w("  uint8_t nram; const MarbleBirdRam *ram; uint16_t nticks; const MarbleBirdTick *ticks;")
        w("  uint16_t nmos; const MarbleBirdMo *mos; uint16_t nlist; const MarbleBirdEntry *list;")
        w("};\n")
        for k, seg in enumerate(segs):
            p = "marble_bird_s%d" % k
            for j, (addr, b) in enumerate(seg["ram"]):
                w("static const uint8_t %s_ram%d[] = {\n%s\n};" % (p, j, c_bytes(b)))
            w("static const MarbleBirdRam %s_ram[] = {" % p)
            for j, (addr, b) in enumerate(seg["ram"]):
                w("  { 0x%06x, %d, %s_ram%d }," % (addr, len(b), p, j))
            w("};")
            mos, lst = [], []

            def add(ms_):
                mos.extend(ms_); return "%d, %d" % (len(ms_), len(mos) - len(ms_))

            def bird(o):
                live, st, x, y, z, sx, sy, tick, loops, cur, hdr, mask, bmo = o
                return ("{ %d, %d, %s, %s, %s, %d, %d, 0x%x, %d, 0x%x, 0x%x, {%s}, %s }"
                        % (live, st, c_int(x), c_int(y), c_int(z), sx, sy, tick, loops, cur, hdr,
                           ",".join(str(v) for v in mask), add(bmo)))
            w("static const MarbleBirdTick %s_ticks[] = {" % p)
            for c6, c7, cam, fr, st in seg["ticks"]:
                pl, flag, dl = st[0], st[-2], st[-1]
                x, y, z, pst, vis, sx, sy, anim, hdr, shard, mask, rng, pmo = pl
                w("  { %d, %d, 0x%08x, %d, %s, %s, %s, %d, %d, %d, %d, 0x%x, 0x%x, 0x%x, {%s}, "
                  "0x%04x, %s," % (c6, c7, cam, fr, c_int(x), c_int(y), c_int(z), pst, vis, sx, sy,
                                   anim, hdr, shard, ",".join(str(v) for v in mask), rng, add(pmo)))
                w("    { %s }, %d, %d, %d }," % (",\n      ".join(bird(o) for o in st[1:11]),
                                                flag, len(dl), len(lst)))
                lst.extend(dl)
            w("};")
            w("static const MarbleBirdMo %s_mos[] = {" % p)
            for k2 in range(0, len(mos), 4):
                w("  " + " ".join("{0x%x,0x%x,0x%x}," % m_ for m_ in mos[k2:k2 + 4]))
            w("};")
            w("static const MarbleBirdEntry %s_list[] = {" % p)
            for k2 in range(0, len(lst), 8):
                w("  " + " ".join("{%d,%d}," % e for e in lst[k2:k2 + 8]))
            w("};\n")
            seg["nmos"], seg["nlist"] = len(mos), len(lst)
        w("static const MarbleBirdSegment marble_bird_segments[] = {")
        for k, seg in enumerate(segs):
            p = "marble_bird_s%d" % k
            w("  { %d, %d, %d, %s_ram, %d, %s_ticks, %d, %s_mos, %d, %s_list }," % (
                seg["race"], seg["frame"], len(seg["ram"]), p, len(seg["ticks"]), p,
                seg["nmos"], p, seg["nlist"], p))
        w("};")
        w("static const uint8_t MARBLE_BIRD_SEGMENTS = %d;\n#endif" % len(segs))


def mini_run(t, trace, i, n):
    """Mini model (marble_enemy.setup: all modelled enemies) from tick start snapshot i for n
    ticks; every tick must equal the next snapshot. Returns (ticks, reads, start RAM)."""
    import marble_enemy as me, marble_mini as mn
    m = me.setup(RecMem(t, bytes(trace[i][1])))
    ticks = []
    for (fr, r, v), (fr2, r2, v2) in zip(trace[i:i + n], trace[i + 1:i + n + 1]):
        inp = ms.death_inputs(r, r2)
        m.events = []; m.sounds = []
        ms.main_tick(m, *inp)
        mo.boxes_and_sort(m)
        d = mn.mini_diff(m, r2, v2)
        if d: raise SystemExit("frame %d: differs from the trace: %s" % (fr2, "; ".join(d[:6])))
        ticks.append((ms.s8(inp[0]), ms.s8(inp[1])) + inp[2:] + (mn.tick_state(m),))
    if len(ticks) != n: raise SystemExit("only %d ticks in the trace" % len(ticks))
    return ticks, m.first_read, bytes(trace[i][1])


def mini_events(ticks):
    ev = {"ticks": len(ticks), "kinds": {}, "live": 0, "drawn": 0, "list max": 0}
    for *_, st in ticks:
        for o in st[1:10]:
            if o[0]: ev["kinds"][o[1]] = ev["kinds"].get(o[1], 0) + 1
        ev["live"] = max(ev["live"], sum(1 for o in st[1:10] if o[0]))
        ev["drawn"] += sum(1 for o in st[1:10] if o[14])
        ev["list max"] = max(ev["list max"], len(st[-1]))
    return ev


def cmd_mnwrite(out, specs):
    import marble_mini as mn
    segs = []
    for spec in specs:
        f = spec.split(":")
        race, path, frame, n = int(f[0]), f[1], int(f[2]), int(f[3])
        trace = ms.load_trace(path)
        i = next(k for k, x in enumerate(trace) if x[0] == frame)
        t = ms.Terrain(race)
        ticks, reads, ram = mini_run(t, trace, i, n)
        seg = {"race": race, "frame": frame, "ram": obj_blocks(ram, reads), "ticks": ticks}
        bad = mn.replay_mnfixture(seg, ms.fixture_mem(seg, t))
        if bad is not None:
            raise SystemExit("race %d frame %d: does not replay from its RAM blocks (tick %d)"
                             % (race, frame, bad))
        print("race %d frame %d: %s, RAM %s" % (race, frame, mini_events(ticks),
              ", ".join("%06x+%d" % (a, len(b)) for a, b in seg["ram"])))
        segs.append(seg)
    with open(out, "w") as f:
        w = lambda s="": print(s, file=f)
        w("// Generated by romconv/internal/marble/tools/make_fixture.py mnwrite; do not edit.")
        w("// Marble silly race minis host test: MAME tick start traces (tools/mame_trace.lua")
        w("// MARBLE_TICK=start, MARBLE_PLACE into the mini field): wander from the race start,")
        w("// follow / flee a player on the field floor (kind 1), collected, cleared and spawned")
        w("// by the camera band, birds and tubes alongside. marble_mini.py main_tick + 0x26f3e")
        w("// sort reproduces them from the initial work RAM blocks and the inputs alone. Derived")
        w("// numbers only. Replay check:")
        w("//   python3 romconv/internal/marble/tools/marble_mini.py mnfixture <this file>")
        w("#ifndef MARBLE_MINIS_TRACE_H\n#define MARBLE_MINIS_TRACE_H\n#include <stdint.h>\n")
        w("// Work RAM 0x400000-0x401fff is 0 except these blocks (68010 address, big-endian).")
        w("struct MarbleMiniRam { uint32_t addr; uint16_t len; const uint8_t *bytes; };")
        w("// One MO of 0x26f3e as in MO RAM: word 0 y << 5 | hflip 0x8000 | height - 1, word 1")
        w("// code | palette set << 11, word 2 x << 5 | priority 0x8000.")
        w("struct MarbleMiniMo { uint16_t w0, code, w2; };")
        w("// Draw list entry in link order (0x4003bc): type, index.")
        w("struct MarbleMiniEntry { uint8_t type, index; };")
        w("// Mini i (0x401890 + 0x28 i) after the tick: live (+0x18), kind (+0x1a), type (+0x25),")
        w("// x, y, z (16.16), sx, sy (+0x20/+0x22), tick (+0x24), steps (+0x1b), cursor (+0x1c")
        w("// anim cursor), dir (+0x26), vx, vy (+0/+4), frame_hdr (rl(cursor), 0 if not in the")
        w("// draw list), its n MOs mos[first ..].")
        w("struct MarbleMiniObj {")
        w("  uint8_t live, kind, type; int32_t x, y, z; int16_t sx, sy;")
        w("  uint8_t tick, steps; uint32_t cursor; uint8_t dir; int32_t vx, vy; uint32_t frame_hdr;")
        w("  uint8_t n; uint16_t first;")
        w("};")
        w("// One main loop tick: inputs c6, c7, camera (long 0x40097c after the camera step), frames")
        w("// (0x400010 at the tick start). Player 1 as MarbleDeathTick, the nine minis, then the")
        w("// draw list list[lfirst .. lfirst + nlist).")
        w("struct MarbleMiniTick {")
        w("  int8_t c6, c7; uint32_t camera, frames;")
        w("  int32_t x, y, z; uint8_t state, vis; int16_t sx, sy;")
        w("  uint32_t anim, frame_hdr, shard_hdr; uint8_t mask[30]; uint16_t rng;")
        w("  uint8_t n; uint16_t first;")
        w("  MarbleMiniObj m[9];")
        w("  uint8_t nlist; uint16_t lfirst;")
        w("};")
        w("struct MarbleMiniSegment {")
        w("  uint8_t race; uint32_t frame;  // race 0-5, MAME frame of the first snapshot")
        w("  uint8_t nram; const MarbleMiniRam *ram; uint16_t nticks; const MarbleMiniTick *ticks;")
        w("  uint16_t nmos; const MarbleMiniMo *mos; uint16_t nlist; const MarbleMiniEntry *list;")
        w("};\n")
        for k, seg in enumerate(segs):
            p = "marble_mini_s%d" % k
            for j, (addr, b) in enumerate(seg["ram"]):
                w("static const uint8_t %s_ram%d[] = {\n%s\n};" % (p, j, c_bytes(b)))
            w("static const MarbleMiniRam %s_ram[] = {" % p)
            for j, (addr, b) in enumerate(seg["ram"]):
                w("  { 0x%06x, %d, %s_ram%d }," % (addr, len(b), p, j))
            w("};")
            mos, lst = [], []

            def add(ms_):
                mos.extend(ms_); return "%d, %d" % (len(ms_), len(mos) - len(ms_))

            def mini(o):
                live, kind, typ, x, y, z, sx, sy, tick, steps, cur, d, vx, vy, hdr, mmo = o
                return ("{ %d, %d, %d, %s, %s, %s, %d, %d, %d, %d, 0x%x, %d, %s, %s, 0x%x, %s }"
                        % (live, kind, typ, c_int(x), c_int(y), c_int(z), sx, sy, tick, steps, cur,
                           d, c_int(vx), c_int(vy), hdr, add(mmo)))
            w("static const MarbleMiniTick %s_ticks[] = {" % p)
            for c6, c7, cam, fr, st in seg["ticks"]:
                pl, dl = st[0], st[-1]
                x, y, z, pst, vis, sx, sy, anim, hdr, shard, mask, rng, pmo = pl
                w("  { %d, %d, 0x%08x, %d, %s, %s, %s, %d, %d, %d, %d, 0x%x, 0x%x, 0x%x, {%s}, "
                  "0x%04x, %s," % (c6, c7, cam, fr, c_int(x), c_int(y), c_int(z), pst, vis, sx, sy,
                                   anim, hdr, shard, ",".join(str(v) for v in mask), rng, add(pmo)))
                w("    { %s }, %d, %d }," % (",\n      ".join(mini(o) for o in st[1:10]),
                                            len(dl), len(lst)))
                lst.extend(dl)
            w("};")
            w("static const MarbleMiniMo %s_mos[] = {" % p)
            for k2 in range(0, len(mos), 4):
                w("  " + " ".join("{0x%x,0x%x,0x%x}," % m_ for m_ in mos[k2:k2 + 4]))
            w("};")
            w("static const MarbleMiniEntry %s_list[] = {" % p)
            for k2 in range(0, len(lst), 8):
                w("  " + " ".join("{%d,%d}," % e for e in lst[k2:k2 + 8]))
            w("};\n")
            seg["nmos"], seg["nlist"] = len(mos), len(lst)
        w("static const MarbleMiniSegment marble_mini_segments[] = {")
        for k, seg in enumerate(segs):
            p = "marble_mini_s%d" % k
            w("  { %d, %d, %d, %s_ram, %d, %s_ticks, %d, %s_mos, %d, %s_list }," % (
                seg["race"], seg["frame"], len(seg["ram"]), p, len(seg["ticks"]), p,
                seg["nmos"], p, seg["nlist"], p))
        w("};")
        w("static const uint8_t MARBLE_MINI_SEGMENTS = %d;\n#endif" % len(segs))


def piston_run(t, trace, i, n):
    """Piston model (marble_enemy.setup: all modelled enemies) from tick start snapshot i for
    n ticks; every tick must equal the next snapshot. Returns (ticks, reads, start RAM)."""
    import marble_enemy as me, marble_piston as mp
    m = me.setup(RecMem(t, bytes(trace[i][1])))
    ticks = []
    for (fr, r, v), (fr2, r2, v2) in zip(trace[i:i + n], trace[i + 1:i + n + 1]):
        inp = ms.death_inputs(r, r2)
        m.events = []; m.sounds = []
        ms.main_tick(m, *inp)
        mo.boxes_and_sort(m)
        d = mp.piston_diff(m, r2, v2)
        if d: raise SystemExit("frame %d: differs from the trace: %s" % (fr2, "; ".join(d[:6])))
        ev = [e[0] for e in m.events if isinstance(e, tuple) and str(e[0])[:6] == "piston"]
        ticks.append((ms.s8(inp[0]), ms.s8(inp[1])) + inp[2:] +
                     (mp.fixture_state(mp.tick_state(m)), ev))
    if len(ticks) != n: raise SystemExit("only %d ticks in the trace" % len(ticks))
    return ticks, m.first_read, bytes(trace[i][1])


def cmd_pswrite(out, specs):
    import marble_piston as mp
    segs = []
    for spec in specs:
        f = spec.split(":")
        race, path, frame, n = int(f[0]), f[1], int(f[2]), int(f[3])
        trace = ms.load_trace(path)
        i = next(k for k, x in enumerate(trace) if x[0] == frame)
        t = ms.Terrain(race)
        run, reads, ram = piston_run(t, trace, i, n)
        ticks = [x[:5] for x in run]
        ev = {}
        for x in run:
            for e in x[5]: ev[e] = ev.get(e, 0) + 1
        seg = {"race": race, "frame": frame, "ram": obj_blocks(ram, reads), "ticks": ticks}
        bad = mp.replay_psfixture(seg, ms.fixture_mem(seg, t))
        if bad is not None:
            raise SystemExit("race %d frame %d: does not replay from its RAM blocks (tick %d)"
                             % (race, frame, bad))
        print("race %d frame %d: %d ticks, events %s, list max %d, RAM %s" % (
            race, frame, n, sorted(ev.items()), max(len(x[4][39]) for x in ticks),
            ", ".join("%06x+%d" % (a, len(b)) for a, b in seg["ram"])))
        segs.append(seg)
    with open(out, "w") as f:
        w = lambda s="": print(s, file=f)
        w("// Generated by romconv/internal/marble/tools/make_fixture.py pswrite; do not edit.")
        w("// Marble aerial race pistons host test: MAME tick start traces (tools/mame_trace.lua")
        w("// MARBLE_TICK=start, MARBLE_PLACE around the pistons): arming on a zone change (the")
        w("// 0x1bb28 cell scratch), pistons rising and falling, a launch, a bounce, a teardown")
        w("// with pistons up. marble_piston.py main_tick + 0x26f3e sort reproduces them from the")
        w("// initial work RAM blocks and the inputs alone. Derived numbers only. Replay check:")
        w("//   python3 romconv/internal/marble/tools/marble_piston.py psfixture <this file>")
        w("#ifndef MARBLE_PISTONS_TRACE_H\n#define MARBLE_PISTONS_TRACE_H\n#include <stdint.h>\n")
        w("// Work RAM 0x400000-0x401fff is 0 except these blocks (68010 address, big-endian).")
        w("struct MarblePistonRam { uint32_t addr; uint16_t len; const uint8_t *bytes; };")
        w("// One MO of 0x26f3e as in MO RAM: word 0 y << 5 | hflip 0x8000 | height - 1, word 1")
        w("// code | palette set << 11, word 2 x << 5 | priority 0x8000.")
        w("struct MarblePistonMo { uint16_t w0, code, w2; };")
        w("// Draw list entry in link order (0x4003bc): type, index.")
        w("struct MarblePistonEntry { uint8_t type, index; };")
        w("// Piston i (0x401650 + 0x10 i) after the tick: timer (+2, 0xffff up), cell x, y")
        w("// (+4/+5), top z word (+6), anim cursor (+8), sx, sy (+0xc/+0xe), frame_hdr (rl(cursor),")
        w("// 0 if not in the draw list), its n MOs mos[first ..].")
        w("struct MarblePistonObj {")
        w("  uint16_t timer; uint8_t cx, cy; int16_t top; uint32_t cursor; int16_t sx, sy;")
        w("  uint32_t frame_hdr; uint8_t n; uint16_t first;")
        w("};")
        w("// One main loop tick: inputs c6, c7, camera (long 0x40097c after the camera step), frames")
        w("// (0x400010 at the tick start). Player 1 as MarbleDeathTick, armed (0x400760), timers")
        w("// (0x400764), the 36 pistons, the draw list list[lfirst .. lfirst + nlist), the cell")
        w("// scratch words 0x400690-0x4006a2.")
        w("struct MarblePistonTick {")
        w("  int8_t c6, c7; uint32_t camera, frames;")
        w("  int32_t x, y, z; uint8_t state, vis; int16_t sx, sy;")
        w("  uint32_t anim, frame_hdr, shard_hdr; uint8_t mask[30]; uint16_t rng;")
        w("  uint8_t n; uint16_t first;")
        w("  uint8_t armed; uint32_t timers;")
        w("  MarblePistonObj p[36];")
        w("  uint8_t nlist; uint16_t lfirst;")
        w("  uint16_t scratch[10];")
        w("};")
        w("struct MarblePistonSegment {")
        w("  uint8_t race; uint32_t frame;  // race 0-5, MAME frame of the first snapshot")
        w("  uint8_t nram; const MarblePistonRam *ram; uint16_t nticks; const MarblePistonTick *ticks;")
        w("  uint16_t nmos; const MarblePistonMo *mos; uint16_t nlist; const MarblePistonEntry *list;")
        w("};\n")
        for k, seg in enumerate(segs):
            p = "marble_piston_s%d" % k
            for j, (addr, b) in enumerate(seg["ram"]):
                w("static const uint8_t %s_ram%d[] = {\n%s\n};" % (p, j, c_bytes(b)))
            w("static const MarblePistonRam %s_ram[] = {" % p)
            for j, (addr, b) in enumerate(seg["ram"]):
                w("  { 0x%06x, %d, %s_ram%d }," % (addr, len(b), p, j))
            w("};")
            mos, lst = [], []

            def add(ms_):
                mos.extend(ms_); return "%d, %d" % (len(ms_), len(mos) - len(ms_))

            def piston(o):
                timer, cx, cy, top, cur, sx, sy, hdr, pmo = o
                return ("{0x%x,%d,%d,%d,0x%x,%d,%d,0x%x,%s}"
                        % (timer, cx, cy, top, cur, sx, sy, hdr, add(pmo)))
            w("static const MarblePistonTick %s_ticks[] = {" % p)
            for c6, c7, cam, fr, st in seg["ticks"]:
                pl, armed, timers = st[:3]
                dl, scratch = st[39], st[40]
                x, y, z, pst, vis, sx, sy, anim, hdr, shard, mask, rng, pmo = pl
                w("  { %d, %d, 0x%08x, %d, %s, %s, %s, %d, %d, %d, %d, 0x%x, 0x%x, 0x%x, {%s}, "
                  "0x%04x, %s, %d, 0x%x," % (c6, c7, cam, fr, c_int(x), c_int(y), c_int(z), pst,
                                             vis, sx, sy, anim, hdr, shard,
                                             ",".join(str(v) for v in mask), rng, add(pmo),
                                             armed, timers))
                w("    { %s },\n    %d, %d, {%s} }," % (
                    ",\n      ".join(piston(o) for o in st[3:39]), len(dl), len(lst),
                    ",".join("0x%x" % v for v in scratch)))
                lst.extend(dl)
            w("};")
            w("static const MarblePistonMo %s_mos[] = {" % p)
            for k2 in range(0, len(mos), 4):
                w("  " + " ".join("{0x%x,0x%x,0x%x}," % m_ for m_ in mos[k2:k2 + 4]))
            if not mos: w("  {0, 0, 0},")
            w("};")
            w("static const MarblePistonEntry %s_list[] = {" % p)
            for k2 in range(0, len(lst), 8):
                w("  " + " ".join("{%d,%d}," % e for e in lst[k2:k2 + 8]))
            w("};\n")
            seg["nmos"], seg["nlist"] = len(mos), len(lst)
        w("static const MarblePistonSegment marble_piston_segments[] = {")
        for k, seg in enumerate(segs):
            p = "marble_piston_s%d" % k
            w("  { %d, %d, %d, %s_ram, %d, %s_ticks, %d, %s_mos, %d, %s_list }," % (
                seg["race"], seg["frame"], len(seg["ram"]), p, len(seg["ticks"]), p,
                seg["nmos"], p, seg["nlist"], p))
        w("};")
        w("static const uint8_t MARBLE_PISTON_SEGMENTS = %d;\n#endif" % len(segs))


# ---------------------------------------------------------------- race start fixture

START_TICKS = 60
# stick per tick (marble.cpp stick(), k = 9): right, down, none; 20 ticks each
START_INPUT = [(-9, -9)] * 20 + [(-9, 9)] * 20 + [(0, 0)] * 20   # (c6, c7)
GOAL_TICKS = 300        # crossing, countdown 0x1e, one +0x57 wrap (256 ticks)
GOAL_V = 0x18000        # 1.5 px per tick across the line
GOAL_SY = 0x80          # camera long puts the goal cell at this sy
DOTS = tuple(range(0x74, 0xbc)) + (0xca,)   # dots 0x1c014 (not in the remake): zeroed


def goal_line(race):
    """First zone line 0x1eb16[race] with a goal side: (cx, cy, horiz, sign, zone): middle
    cell of the line, +1/-1 step along y (horizontal line) or x towards zone 0xff, zone of
    the other side."""
    a0 = ms.rom_l(0x1eb16 + 4 * race)
    while True:
        b = [ms.PROG[a0 + k] for k in range(6)]
        if b[3] == 0xff and b[4] == 0xff: raise SystemExit("race %d: no goal line" % race)
        if 0xff in (b[3], b[4]):
            x, y, end = ms.s8(b[0]), ms.s8(b[1]), ms.s8(b[2])
            horiz = x < 0
            mid = ((abs(x) if horiz else abs(y)) + end) // 2
            cx, cy = (mid, abs(y)) if horiz else (abs(x), mid)
            return cx, cy, horiz, 1 if b[4] == 0xff else -1, b[3] if b[4] == 0xff else b[4]
        a0 += 6


def place_goal(m, race):
    """Marble on the goal line cell centre, z on the terrain, rolling GOAL_V towards the goal
    side, zone of the other side. Returns the camera long (sign extended word) for GOAL_SY."""
    cx, cy, horiz, sign, zone = goal_line(race)
    m.wl(P + 0xc, (cx * 8 + 4) << 16); m.wl(P + 0x10, (cy * 8 + 4) << 16)
    m.ww(0x400698, 0xffff); m.ww(0x400696, 0xffff); ms.posupd_1bab2(m, P)
    z = ms.height_1cc62(m) & 0xffffffff
    m.wl(P + 0x14, z); m.wl(P + 0x2a, z); m.wb(P + 0x1b, zone); m.wb(P + 0x36, 0)
    m.wl(P + (4 if horiz else 0), (sign * GOAL_V) & 0xffffffff)
    xy = (m.sw(P + 0xc) + m.sw(P + 0x10)) >> 1
    return ms.s16(GOAL_SY - m.rw(P + 0x14) - 0x54 + xy) & 0xffffffff


def start_crc(m):
    """crc32 of player 1 (0xe2 bytes, DOTS zeroed) and the RNG word 0x4003a6."""
    import zlib
    a = P - 0x400000
    b = bytearray(m.ram[a:a + 0xe2])
    for o in DOTS: b[o] = 0
    return zlib.crc32(bytes(b) + bytes(m.ram[0x3a6:0x3a8]))


def start_run(race, goal):
    """begin_race, then START_TICKS main_ticks with START_INPUT at the start camera, or (goal)
    the goal placement and GOAL_TICKS idle main_ticks. Returns (camera, crc per tick, crc
    after the start, states seen)."""
    import marble_enemy
    m = marble_enemy.setup(ms.Mem(ms.Terrain(race), bytearray(0x2000)))  # all enemies, as the remake
    ms.begin_race(m, race)
    crc0 = start_crc(m)
    cam = place_goal(m, race) if goal else m.rl(0x40097c)
    crcs, states = [], set()
    for k in range(GOAL_TICKS if goal else START_TICKS):
        c6, c7 = (0, 0) if goal else START_INPUT[k]
        ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam, k + 1)
        crcs.append(start_crc(m)); states.add(m.rb(P + 0x1a))
    return cam, crcs, crc0, states


def cmd_rwrite(out):
    runs = []
    for goal in (0, 1):
        for race in range(6):
            cam, crcs, crc0, states = start_run(race, goal)
            if goal and 6 not in states: raise SystemExit("race %d: goal not reached" % race)
            print("race %d %s: camera %08x, %d ticks, states %s" % (
                race, "goal" if goal else "start", cam, len(crcs), sorted(states)))
            runs.append((race, goal, cam, crc0, crcs))
    with open(out, "w") as f:
        w = lambda s="": print(s, file=f)
        w("// Generated by romconv/internal/marble/tools/make_fixture.py rwrite; do not edit.")
        w("// Remake race start against the model (marble_sim.py begin_race, main_tick with deaths,")
        w("// objects, sprites on), every race: START_TICKS ticks of stick input from the start,")
        w("// and the goal: marble placed on the goal line (make_fixture.py place_goal), idle")
        w("// GOAL_TICKS ticks (crossing, goal countdown, one +0x57 wrap). Inputs: c6/c7 (start")
        w("// runs), camera long, 0x400010 = tick + 1. Per tick crc32 of player 1 (0xe2 bytes,")
        w("// dots +0x74-+0xbb and +0xca zeroed: 0x1c014 not ported) and the RNG word 0x4003a6.")
        w("#ifndef MARBLE_START_TRACE_H\n#define MARBLE_START_TRACE_H\n#include <stdint.h>\n")
        w("static const uint16_t MARBLE_START_TICKS = %d, MARBLE_GOAL_TICKS = %d;" % (
            START_TICKS, GOAL_TICKS))
        w("static const int32_t MARBLE_GOAL_V = 0x%x;    // px per tick, 16.16" % GOAL_V)
        w("static const int16_t MARBLE_GOAL_SY = 0x%x;" % GOAL_SY)
        w("static const uint8_t MARBLE_DOTS_LO = 0x74, MARBLE_DOTS_HI = 0xbc, MARBLE_DOTS_K = 0xca;")
        w("static const int8_t marble_start_input[%d][2] = {  // c6, c7" % START_TICKS)
        for i in range(0, START_TICKS, 10):
            w("  " + " ".join("{ %d, %d }," % v for v in START_INPUT[i:i + 10]))
        w("};")
        w("struct MarbleStartRun {")
        w("  uint8_t race, goal;  // goal: placed on the goal line")
        w("  uint32_t camera;     // camera long, every tick")
        w("  uint32_t crc0;       // after begin_race")
        w("  const uint32_t *crc; // per tick")
        w("};\n")
        for k, (race, goal, cam, crc0, crcs) in enumerate(runs):
            w("static const uint32_t marble_start_r%d[] = {" % k)
            for i in range(0, len(crcs), 8):
                w("  " + ", ".join("0x%08x" % c for c in crcs[i:i + 8]) + ",")
            w("};")
        w("\nstatic const MarbleStartRun marble_start_runs[] = {")
        for k, (race, goal, cam, crc0, crcs) in enumerate(runs):
            w("  { %d, %d, 0x%08x, 0x%08x, marble_start_r%d }," % (race, goal, cam, crc0, k))
        w("};")
        w("static const uint8_t MARBLE_START_RUNS = %d;\n#endif" % len(runs))


def c_bytes(b):
    return ",\n".join("  " + ", ".join("0x%02x" % v for v in b[k:k + 16])
                      for k in range(0, len(b), 16))


def c_int(v):
    return "%d" % v if v < 0 else "0x%08x" % v


def cmd_write(out, specs):
    segs = []
    for spec in specs:
        race, path, frame, n = spec.split(":")
        race, frame, n = int(race), int(frame), int(n)
        trace = ms.load_trace(path)
        i = next(k for k, x in enumerate(trace) if x[0] == frame)
        seg, end, stats = segment(ms.Terrain(race), trace, i, n)
        if len(seg["ticks"]) != n:
            raise SystemExit("race %d frame %d: only %d ticks (%s)" % (race, frame, len(seg["ticks"]), end))
        print("race %d frame %d: %d ticks, %s, %s, RAM %s" % (race, frame, n, end, stats,
              ", ".join("%06x+%d" % (a, len(b)) for a, b in seg["ram"])))
        segs.append(seg)
    with open(out, "w") as f:
        w = lambda s="": print(s, file=f)
        w("// Generated by romconv/internal/marble/tools/make_fixture.py; do not edit.")
        w("// Marble physics host test: MAME trace segments (tools/mame_trace.lua) the reference")
        w("// model tools/marble_sim.py reproduces without level object slots. Derived numbers")
        w("// only: trackball deltas, positions, initial work RAM bytes. Replay check:")
        w("//   python3 romconv/internal/marble/tools/marble_sim.py fixture <this file>")
        w("#ifndef MARBLE_TRACE_H\n#define MARBLE_TRACE_H\n#include <stdint.h>\n")
        w("// Work RAM 0x400000-0x401fff is 0 except these blocks (68010 address, big-endian).")
        w("struct MarbleTraceRam { uint32_t addr; uint16_t len; const uint8_t *bytes; };")
        w("// One logic tick: tick(c6, c7), then player 0x400018 has x, y, z (16.16, +0x0c/10/14)")
        w("// and state (+0x1a). Segments end before the death tick: the break choice needs the")
        w("// projected sy (no camera word here), 0x25bae(4) respawns in that tick; the deaths")
        w("// fixture (test_marble_deaths) covers deaths.")
        w("struct MarbleTraceTick { int8_t c6, c7; int32_t x, y, z; uint8_t state; };")
        w("struct MarbleTraceSegment {")
        w("  uint8_t race; uint32_t frame;  // race 0-5, MAME frame of the first snapshot")
        w("  uint8_t nram; const MarbleTraceRam *ram; uint16_t nticks; const MarbleTraceTick *ticks;")
        w("};\n")
        for k, seg in enumerate(segs):
            p = "marble_trace_s%d" % k
            for j, (addr, b) in enumerate(seg["ram"]):
                w("static const uint8_t %s_ram%d[] = {\n%s\n};" % (p, j, c_bytes(b)))
            w("static const MarbleTraceRam %s_ram[] = {" % p)
            for j, (addr, b) in enumerate(seg["ram"]):
                w("  { 0x%06x, %d, %s_ram%d }," % (addr, len(b), p, j))
            w("};")
            w("static const MarbleTraceTick %s_ticks[] = {" % p)
            for c6, c7, x, y, z, st in seg["ticks"]:
                w("  { %d, %d, %s, %s, %s, %d }," % (c6, c7, c_int(x), c_int(y), c_int(z), st))
            w("};\n")
        w("static const MarbleTraceSegment marble_trace_segments[] = {")
        for k, seg in enumerate(segs):
            p = "marble_trace_s%d" % k
            w("  { %d, %d, %d, %s_ram, %d, %s_ticks }," % (seg["race"], seg["frame"],
              len(seg["ram"]), p, len(seg["ticks"]), p))
        w("};")
        w("static const uint8_t MARBLE_TRACE_SEGMENTS = %d;\n#endif" % len(segs))


def cmd_owrite(out, specs):
    segs = []
    for spec in specs:
        race, path, frame, n = spec.split(":")
        race, frame, n = int(race), int(frame), int(n)
        trace = ms.load_trace(path)
        i = next(k for k, x in enumerate(trace) if x[0] == frame)
        seg, end, ev = obj_segment(ms.Terrain(race), trace, i, n)
        if len(seg["ticks"]) != n:
            raise SystemExit("race %d frame %d: only %d ticks (%s)" % (race, frame,
                                                                       len(seg["ticks"]), end))
        bad = obj_check(seg)
        if bad is not None:
            raise SystemExit("race %d frame %d: does not replay from its RAM blocks (tick %d)"
                             % (race, frame, bad))
        print("race %d frame %d: %d ticks, %s, RAM %s" % (race, frame, n, ev,
              ", ".join("%06x+%d" % (a, len(b)) for a, b in seg["ram"])))
        segs.append(seg)
    with open(out, "w") as f:
        w = lambda s="": print(s, file=f)
        w("// Generated by romconv/internal/marble/tools/make_fixture.py owrite; do not edit.")
        w("// Marble level object host test: MAME trace segments (tools/mame_trace.lua) that the")
        w("// reference model tools/marble_sim.py reproduces with level object slots, dynamic")
        w("// terrain codes and occlusion masks running free. Derived numbers only. Replay check:")
        w("//   python3 romconv/internal/marble/tools/marble_sim.py objfixture <this file>")
        w("#ifndef MARBLE_OBJECTS_TRACE_H\n#define MARBLE_OBJECTS_TRACE_H\n#include <stdint.h>\n")
        w("// Work RAM 0x400000-0x401fff is 0 except these blocks (68010 address, big-endian).")
        w("struct MarbleObjRam { uint32_t addr; uint16_t len; const uint8_t *bytes; };")
        w("// One logic tick, as tick(c6, c7, camera) of the model: the player update, the slot")
        w("// update 0x12fd0, the queued animation frame 0x1344c, then the camera long")
        w("// (0x40097c, an input like the trackball deltas) with the band spawns 0x144e4.")
        w("// Then x, y, z (16.16) and state of the player 0x400018 (+0x0c/+0x10/+0x14/+0x1a),")
        w("// vis (+0x1c: 0 = marble hidden), mask (the 30 bytes +0x38-+0x55: 5 entries of big")
        w("// endian code, x, y; code 0 ends the list), slot_crc (crc32 of the 2150 bytes of")
        w("// the 25 slots, work RAM 0x400a9c-0x401301) and dyn_crc (crc32 of the 256 bytes of")
        w("// the 128 dynamic code words, 0x40076e-0x40086d), both over the big-endian RAM bytes.")
        w("struct MarbleObjTick {")
        w("  int8_t c6, c7; uint32_t camera; int32_t x, y, z; uint8_t state, vis;")
        w("  uint8_t mask[30]; uint32_t slot_crc, dyn_crc;")
        w("};")
        w("struct MarbleObjSegment {")
        w("  uint8_t race; uint32_t frame;  // race 0-5, MAME frame of the first snapshot")
        w("  uint8_t nram; const MarbleObjRam *ram; uint16_t nticks; const MarbleObjTick *ticks;")
        w("};\n")
        for k, seg in enumerate(segs):
            p = "marble_obj_s%d" % k
            for j, (addr, b) in enumerate(seg["ram"]):
                w("static const uint8_t %s_ram%d[] = {\n%s\n};" % (p, j, c_bytes(b)))
            w("static const MarbleObjRam %s_ram[] = {" % p)
            for j, (addr, b) in enumerate(seg["ram"]):
                w("  { 0x%06x, %d, %s_ram%d }," % (addr, len(b), p, j))
            w("};")
            w("static const MarbleObjTick %s_ticks[] = {" % p)
            for c6, c7, cam, x, y, z, state, vis, masks, scrc, dcrc in seg["ticks"]:
                w("  { %d, %d, 0x%08x, %s, %s, %s, %d, %d, {%s}, 0x%08x, 0x%08x }," % (
                    c6, c7, cam, c_int(x), c_int(y), c_int(z), state, vis,
                    ",".join(str(v) for v in masks), scrc, dcrc))
            w("};\n")
        w("static const MarbleObjSegment marble_objects_segments[] = {")
        for k, seg in enumerate(segs):
            p = "marble_obj_s%d" % k
            w("  { %d, %d, %d, %s_ram, %d, %s_ticks }," % (seg["race"], seg["frame"],
              len(seg["ram"]), p, len(seg["ticks"]), p))
        w("};")
        w("static const uint8_t MARBLE_OBJECTS_SEGMENTS = %d;\n#endif" % len(segs))


def main(argv):
    if len(argv) == 3 and argv[1] == "rwrite":
        cmd_rwrite(argv[2])
        return 0
    if len(argv) >= 4 and argv[1] == "pswrite":
        cmd_pswrite(argv[2], argv[3:])
        return 0
    if len(argv) >= 4 and argv[1] == "mnwrite":
        cmd_mnwrite(argv[2], argv[3:])
        return 0
    if len(argv) >= 4 and argv[1] == "twrite":
        cmd_twrite(argv[2], argv[3:])
        return 0
    if len(argv) >= 4 and argv[1] == "awrite":
        cmd_awrite(argv[2], argv[3:])
        return 0
    if len(argv) >= 4 and argv[1] == "bdwrite":
        cmd_bdwrite(argv[2], argv[3:])
        return 0
    if len(argv) >= 4 and argv[1] == "acwrite":
        cmd_acwrite(argv[2], argv[3:])
        return 0
    if len(argv) >= 4 and argv[1] == "slwrite":
        cmd_slwrite(argv[2], argv[3:])
        return 0
    if len(argv) >= 4 and argv[1] == "stwrite":
        cmd_stwrite(argv[2], argv[3:])
        return 0
    if len(argv) >= 4 and argv[1] == "tbwrite":
        cmd_tbwrite(argv[2], argv[3:])
        return 0
    if len(argv) >= 4 and argv[1] == "fwrite":
        import marble_flow
        marble_flow.write_fixture(argv[2], argv[3:])
        return 0
    if len(argv) >= 4 and argv[1] == "dwrite":
        cmd_dwrite(argv[2], argv[3:])
        return 0
    if len(argv) >= 4 and argv[1] == "ssearch":
        cmd_ssearch(argv[2], int(argv[3]), int(argv[4]) if len(argv) > 4 else 300)
        return 0
    if len(argv) >= 4 and argv[1] == "swrite":
        cmd_swrite(argv[2], argv[3:])
        return 0
    if len(argv) >= 4 and argv[1] == "osearch":
        cmd_osearch(argv[2], int(argv[3]), int(argv[4]) if len(argv) > 4 else 300)
        return 0
    if len(argv) >= 4 and argv[1] == "owrite":
        cmd_owrite(argv[2], argv[3:])
        return 0
    if len(argv) >= 4 and argv[1] == "search":
        cmd_search(argv[2], int(argv[3]), int(argv[4]) if len(argv) > 4 else 300)
        return 0
    if len(argv) >= 4 and argv[1] == "write":
        cmd_write(argv[2], argv[3:])
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
