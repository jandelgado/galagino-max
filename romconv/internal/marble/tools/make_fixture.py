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

def death_run(t, trace, i, n):
    """Death model (marble_sim.main_tick) from tick start snapshot i for n ticks, inputs
    only from the trace; every tick must equal the next snapshot. Returns (ticks, reads)."""
    m = RecMem(t, trace[i][1])
    m.deaths = m.objects = m.sprites = True
    ticks = []
    for (fr, r, v), (fr2, r2, v2) in zip(trace[i:i + n], trace[i + 1:i + n + 1]):
        inp = ms.death_inputs(r, r2)
        m.events = []; m.sounds = []
        ms.main_tick(m, *inp)
        d = ms.death_diff(m, r2, v2)
        if d: raise SystemExit("frame %d: differs from the trace: %s" % (fr2, "; ".join(d[:6])))
        ticks.append((ms.s8(inp[0]), ms.s8(inp[1])) + inp[2:] + (ms.death_state(m),))
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


def cmd_dwrite(out, specs):
    segs = []
    for spec in specs:
        race, path, frame, n = spec.split(":")
        race, frame, n = int(race), int(frame), int(n)
        trace = ms.load_trace(path)
        i = next(k for k, x in enumerate(trace) if x[0] == frame)
        t = ms.Terrain(race)
        ticks, reads = death_run(t, trace, i, n)
        seg = {"race": race, "frame": frame, "ram": obj_blocks(trace[i][1], reads),
               "ticks": [(c6, c7, cam, fr, st) for c6, c7, cam, fr, st in ticks]}
        bad = ms.replay_dfixture(seg, ms.fixture_mem(seg, t))
        if bad is not None:
            raise SystemExit("race %d frame %d: does not replay from its RAM blocks (tick %d)"
                             % (race, frame, bad))
        print("race %d frame %d: %s, RAM %s" % (race, frame, death_events(ticks),
              ", ".join("%06x+%d" % (a, len(b)) for a, b in seg["ram"])))
        segs.append(seg)
    with open(out, "w") as f:
        w = lambda s="": print(s, file=f)
        w("// Generated by romconv/internal/marble/tools/make_fixture.py dwrite; do not edit.")
        w("// Marble player death host test: MAME tick start traces (tools/mame_trace.lua")
        w("// MARBLE_TICK=start) between MARBLE_PLACE teleports: dizzy (state 1), shatter (2),")
        w("// lost (4), respawn. marble_sim.py main_tick reproduces them from the initial work RAM")
        w("// blocks and the inputs alone. Derived numbers only. Replay check:")
        w("//   python3 romconv/internal/marble/tools/marble_sim.py dfixture <this file>")
        w("#ifndef MARBLE_DEATHS_TRACE_H\n#define MARBLE_DEATHS_TRACE_H\n#include <stdint.h>\n")
        w("// Work RAM 0x400000-0x401fff is 0 except these blocks (68010 address, big-endian).")
        w("struct MarbleDeathRam { uint32_t addr; uint16_t len; const uint8_t *bytes; };")
        w("// One player 1 MO of 0x26f3e (type 1) as in MO RAM: word 0 y << 5 | hflip 0x8000 |")
        w("// height - 1, word 1 code | palette set << 11, word 2 x << 5 | priority 0x8000.")
        w("struct MarbleDeathMo { uint16_t w0, code, w2; };")
        w("// One main loop tick (marble_sim.py main_tick): inputs c6, c7 (trackball deltas of")
        w("// the tick, +0xc6/+0xc7), camera (long 0x40097c after the camera step), frames")
        w("// (0x400010 at the tick start). Then player 1: x, y, z (16.16), state (+0x1a), vis")
        w("// (+0x1c), sx, sy (+0x1e/+0x20, MO position sx + 0x18, sy + 0x10), anim (+0x5a),")
        w("// frame_hdr (frame header rl(anim) drawn in states 1, 2, 5, 9 with palette set 0,")
        w("// else 0), shard_hdr (shatter overlay rl(+0x62) when +0x67, set 2, else 0), mask")
        w("// (+0x38-+0x55: 5 entries of big endian code, x, y; mask codes have bit 14, the")
        w("// state 4 sparkle 0x13ade writes star entries without it), rng (0x4003a6), then")
        w("// the n player MOs mos[first .. first + n).")
        w("struct MarbleDeathTick {")
        w("  int8_t c6, c7; uint32_t camera, frames;")
        w("  int32_t x, y, z; uint8_t state, vis; int16_t sx, sy;")
        w("  uint32_t anim, frame_hdr, shard_hdr; uint8_t mask[30]; uint16_t rng;")
        w("  uint8_t n; uint16_t first;")
        w("};")
        w("struct MarbleDeathSegment {")
        w("  uint8_t race; uint32_t frame;  // race 0-5, MAME frame of the first snapshot")
        w("  uint8_t nram; const MarbleDeathRam *ram; uint16_t nticks; const MarbleDeathTick *ticks;")
        w("  uint16_t nmos; const MarbleDeathMo *mos;")
        w("};\n")
        for k, seg in enumerate(segs):
            p = "marble_death_s%d" % k
            for j, (addr, b) in enumerate(seg["ram"]):
                w("static const uint8_t %s_ram%d[] = {\n%s\n};" % (p, j, c_bytes(b)))
            w("static const MarbleDeathRam %s_ram[] = {" % p)
            for j, (addr, b) in enumerate(seg["ram"]):
                w("  { 0x%06x, %d, %s_ram%d }," % (addr, len(b), p, j))
            w("};")
            first, mos = 0, []
            w("static const MarbleDeathTick %s_ticks[] = {" % p)
            for c6, c7, cam, fr, (x, y, z, st, vis, sx, sy, anim, hdr, shard, mask, rng,
                                  mo) in seg["ticks"]:
                w("  { %d, %d, 0x%08x, %d, %s, %s, %s, %d, %d, %d, %d, 0x%x, 0x%x, 0x%x, {%s}, "
                  "0x%04x, %d, %d }," % (c6, c7, cam, fr, c_int(x), c_int(y), c_int(z), st, vis,
                                         sx, sy, anim, hdr, shard,
                                         ",".join(str(v) for v in mask), rng, len(mo), first))
                first += len(mo); mos += mo
            w("};")
            w("static const MarbleDeathMo %s_mos[] = {" % p)
            for k2 in range(0, len(mos), 4):
                w("  " + " ".join("{0x%x,0x%x,0x%x}," % mo for mo in mos[k2:k2 + 4]))
            w("};\n")
            seg["nmos"] = len(mos)
        w("static const MarbleDeathSegment marble_deaths_segments[] = {")
        for k, seg in enumerate(segs):
            p = "marble_death_s%d" % k
            w("  { %d, %d, %d, %s_ram, %d, %s_ticks, %d, %s_mos }," % (
                seg["race"], seg["frame"], len(seg["ram"]), p, len(seg["ticks"]), p,
                seg["nmos"], p))
        w("};")
        w("static const uint8_t MARBLE_DEATHS_SEGMENTS = %d;\n#endif" % len(segs))


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
