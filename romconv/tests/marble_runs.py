"""Marble model runs of test_games_marble: every MAME fixture replay, race start and race sweep
of every phase on a Mem class. The converter emits the whole tables of convert.prog_tables();
these runs on a recording ROM prove the tables cover every byte the model reads (the remake
reads the same tables by 68010 address and aborts on any other)."""
import zlib

from romconv.games.marble import convert as c
from romconv.games.marble import data as md
from romconv.games.marble.sim import marble_acid as ac
from romconv.games.marble.sim import marble_bird as bd
from romconv.games.marble.sim import marble_enemy as me
from romconv.games.marble.sim import marble_mini as mn
from romconv.games.marble.sim import marble_mo as mo
from romconv.games.marble.sim import marble_piston as pp
from romconv.games.marble.sim import marble_sim as ms
from romconv.games.marble.sim import marble_slinky as sl


class RecProg(bytearray):
    """Program ROM that records the byte addresses read (indexing and slices) into reads."""
    def __init__(self, data, reads):
        super().__init__(data)
        self.reads = reads

    def __getitem__(self, k):
        if isinstance(k, slice):
            self.reads.update(range(*k.indices(len(self))))
        else:
            self.reads.add(k)
        return bytearray.__getitem__(self, k)


class RecMem(ms.Mem):
    """Mem that records the slapstic window addresses the physics reads into slap (race -> set)."""
    def __init__(self, terrain, ram, rom=None, slap=None):
        super().__init__(terrain, ram, rom)
        self.slap = slap

    def rb(self, a):
        a &= 0xFFFFFF
        if 0x80000 <= a < 0x88000:
            self.slap.setdefault(self.t.race, set()).add(a)
        return super().rb(a)


def _physics_runs(run, mem, terrains, objects=False):
    """Race starts of all races and every Phase 2 fixture tick, on Mem class mem (with
    objects: slots, projection and masks on). Returns positions."""
    out = []
    for n in range(md.LEVELS):
        m = mem(terrains[n], bytes(0x2000))
        m.objects = m.sprites = objects
        ms.start_259b4(m, n)
        ms.snapshot_start(m)
        out.append(tuple(m.sl(ms.P + o) for o in (0xC, 0x10, 0x14)))
    for seg in ms.load_fixture(run.trace(c.TRACE_FIXTURE, "write")):
        m = ms.fixture_mem(seg, terrains[seg["race"]], mem)
        m.objects = m.sprites = objects
        if ms.replay_fixture(seg, m) is not None:
            raise AssertionError("marble: fixture replay differs, run marble_sim.py fixture")
        out.append(tuple(m.sl(ms.P + o) for o in (0xC, 0x10, 0x14)))
    return out


_OBJ_IDLE_TICKS = 200   # past the practice gate wait (150 ticks) and its first frames


def _object_runs(run, mem, terrains):
    """Phase 3: level init (objects_init) of all races plus idle ticks on a cleared RAM, and
    every object fixture segment, objects and sprites on. Returns RAM crc32 per run."""
    out = []
    for n in range(md.LEVELS):
        m = mem(terrains[n], bytes(0x2000))
        m.objects = m.sprites = True
        ms.start_259b4(m, n)
        ms.snapshot_start(m)
        ms.objects_init(m, n)
        for _ in range(_OBJ_IDLE_TICKS): ms.tick(m, 0, 0)
        out.append(zlib.crc32(bytes(m.ram)))
    for seg in ms.load_objfixture(run.trace(c.OBJ_FIXTURE, "owrite")):
        m = ms.fixture_mem(seg, terrains[seg["race"]], mem)
        if ms.replay_objfixture(seg, m) is not None:
            raise AssertionError("marble: object fixture replay differs, run marble_sim.py objfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def _sprite_runs(run, mem, terrains):
    """Phase 4a: every sprite fixture segment (objects, draw list, sort, publish) on Mem class
    mem. Returns crc32 of the published lists per segment."""
    out = []
    for seg in mo.load_spritefixture(run.trace(c.SPRITE_FIXTURE, "swrite")):
        m = ms.fixture_mem(seg, terrains[seg["race"]], mem)
        bad, got = mo.replay_spritefixture(seg, m, run.sprites[seg["race"]])
        if bad is not None:
            raise AssertionError("marble: sprite fixture replay differs, run marble_mo.py spritefixture")
        out.append(zlib.crc32(repr(got).encode()))
    return out + c.race_sweeps(run, mem, terrains)


def _death_headers(run, m):
    """Frame headers the remake draws for the player (as ms.death_state, read from ms.PROG):
    rl(rl(+0x5a)) in states 1/2/5/9, the overlay rl(rl(+0x62)) when +0x67, else 0. Fails on a
    header outside the death set."""
    drawn = m.rb(ms.P + 0x1c)
    hdr = m.rl(m.rl(ms.P + 0x5a)) if drawn and m.rb(ms.P + 0x1a) in (1, 2, 5, 9) else 0
    shard = m.rl(m.rl(ms.P + 0x62)) if drawn and m.rb(ms.P + 0x67) else 0
    for h in (hdr, shard):
        if h and h not in run.death.ids:
            raise AssertionError(f"marble: player frame {h:05x} not in the death set")
    return hdr, shard


def _death_runs(run, mem, terrains, full):
    """Phase 4b-0: every death fixture segment (main tick with deaths, objects, sprites) on Mem
    class mem. The check values read the program ROM full (the player MO model reads frame
    bytes the remake replaces by the death set). Returns crc32 of the work RAM per segment."""
    out = []
    for seg in ms.load_dfixture(run.trace(c.DEATH_FIXTURE, "dwrite")):
        m = ms.fixture_mem(seg, terrains[seg["race"]], mem)
        m.deaths = m.objects = m.sprites = True
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            hdrs = _death_headers(run, m)
            got = ms.death_state(m.view(full))
            if got != want or hdrs != got[8:10]:
                raise AssertionError("marble: death fixture replay differs, run marble_sim.py dfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def _steelie_runs(run, mem, terrains, full):
    """Phase 4b-1: every steelie fixture segment and the steelie race sweep of every race
    (main tick with deaths, objects, sprites, draw list, steelies) on Mem class mem. Check
    values read the program ROM full (the MO model reads frame bytes the remake replaces by
    the sprite sets); every steelie frame drawn must be in its race set (0x21f36) or the
    death set (shatter). Returns crc32 of the work RAM per run."""

    def check(m, race):
        got = me.tick_state(m.view(full))
        for s in got[1:3]:
            hdr, shard = s[9], s[10]
            if hdr == mo.STEELIE_FRAME and hdr not in run.sprites[race].ids:
                raise AssertionError(f"marble: race {race} steelie frame not in its sprite set")
            if hdr not in (0, mo.STEELIE_FRAME) and hdr not in run.death.ids or \
                    shard and shard not in run.death.ids:
                raise AssertionError(f"marble: steelie frame {hdr:05x}/{shard:05x} not in the death set")
        return got

    out = []
    for seg in me.load_sfixture(run.trace(c.STEELIE_FIXTURE, "stwrite")):
        m = me.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            mo.boxes_and_sort(m)
            if check(m, seg["race"]) != want:
                raise AssertionError("marble: steelie fixture replay differs, run marble_enemy.py sfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    for n in range(md.LEVELS):
        m = me.setup(mem(terrains[n], bytes(0x2000)))
        me.sweep(m, n)
        check(m, n)
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def _slinky_runs(run, mem, terrains, full):
    """Phase 4b-2: every slinky fixture segment and the slinky race sweep of every race (main
    tick with deaths, objects, sprites, draw list, steelies, slinkies) on Mem class mem. Every
    slinky frame drawn must be in the death set. Returns crc32 of the work RAM per run."""

    def check(m, race):
        got = sl.tick_state(m.view(full))
        for s_ in got[3:7]:
            if s_[11] and s_[11] not in run.death.ids:
                raise AssertionError(f"marble: slinky frame {s_[11]:05x} not in the death set")
        return got

    out = []
    for seg in sl.load_slfixture(run.trace(c.SLINKY_FIXTURE, "slwrite")):
        m = sl.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            mo.boxes_and_sort(m)
            if check(m, seg["race"]) != want:
                raise AssertionError("marble: slinky fixture replay differs, run marble_slinky.py slfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    for n in range(md.LEVELS):
        m = sl.setup(mem(terrains[n], bytes(0x2000)))
        sl.sweep(m, n)
        check(m, n)
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def _acid_runs(run, mem, terrains, full):
    """Phase 4b-3: every acid fixture segment and the acid race sweep of every race (main tick
    with all modelled enemies) on Mem class mem. Every acid frame drawn must be in the death
    set. Returns crc32 of the work RAM per run."""

    def check(m):
        got = ac.tick_state(m.view(full))
        for a_ in got[1:8]:
            if a_[9] and a_[9] not in run.death.ids:
                raise AssertionError(f"marble: acid frame {a_[9]:05x} not in the death set")
        return got

    out = []
    for seg in ac.load_acfixture(run.trace(c.ACID_FIXTURE, "acwrite")):
        m = me.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            mo.boxes_and_sort(m)
            if check(m) != want:
                raise AssertionError("marble: acid fixture replay differs, run marble_acid.py acfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    for n in range(md.LEVELS):
        m = me.setup(mem(terrains[n], bytes(0x2000)))
        ac.sweep(m, n)
        check(m)
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def _bird_runs(run, mem, terrains, full):
    """Phase 4b-4a: every bird fixture segment and the bird race sweep of every race (main tick
    with all modelled enemies) on Mem class mem. Every bird frame drawn must be in the death
    set. Returns crc32 of the work RAM per run."""

    def check(m):
        got = bd.tick_state(m.view(full))
        for b in got[1:11]:
            if b[10] and b[10] not in run.death.ids:
                raise AssertionError(f"marble: bird frame {b[10]:05x} not in the death set")
        return got

    out = []
    for seg in bd.load_bdfixture(run.trace(c.BIRD_FIXTURE, "bdwrite")):
        m = me.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            mo.boxes_and_sort(m)
            if check(m) != want:
                raise AssertionError("marble: bird fixture replay differs, run marble_bird.py bdfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    for n in range(md.LEVELS):
        m = me.setup(mem(terrains[n], bytes(0x2000)))
        bd.sweep(m, n)
        check(m)
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def _tube_runs(run, mem, terrains, full):
    """Tubes: every tube fixture segment (main tick with all enemies, the RNG word an input); the
    check values read the program ROM full, as death_runs. Returns crc32 of the work RAM per
    segment."""
    out = []
    for seg in ms.load_dfixture(run.trace(c.TUBE_FIXTURE, "twrite"), True):
        m = me.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, rng, want in seg["ticks"]:
            m.ww(0x4003a6, rng)
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            got = ms.death_state(m.view(full))
            if got != want:
                raise AssertionError("marble: tube fixture replay differs, run marble_sim.py tfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def _aerial_runs(run, mem, terrains, full):
    """Aerial objects: every aerial fixture segment (main tick with all enemies); the check
    values read the program ROM full, as death_runs. Returns crc32 of the work RAM per
    segment."""
    out = []
    for seg in ms.load_dfixture(run.trace(c.AERIAL_FIXTURE, "awrite"), False, "marble_aerials_segments"):
        m = me.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            got = ms.death_state(m.view(full))
            if got != want:
                raise AssertionError("marble: aerial fixture replay differs, run marble_sim.py afixture")
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def _mini_runs(run, mem, terrains, full):
    """Phase 4b-4b: every mini fixture segment and the mini race sweep of every race (main tick
    with all modelled enemies) on Mem class mem. Every mini frame drawn must be in the death
    set. Returns crc32 of the work RAM per run."""

    def check(m):
        got = mn.tick_state(m.view(full))
        for o in got[1:10]:
            if o[14] and o[14] not in run.death.ids:
                raise AssertionError(f"marble: mini frame {o[14]:05x} not in the death set")
        return got

    out = []
    for seg in mn.load_mnfixture(run.trace(c.MINI_FIXTURE, "mnwrite")):
        m = me.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            mo.boxes_and_sort(m)
            if check(m) != want:
                raise AssertionError("marble: mini fixture replay differs, run marble_mini.py mnfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    for n in range(md.LEVELS):
        m = me.setup(mem(terrains[n], bytes(0x2000)))
        mn.sweep(m, n)
        check(m)
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def _piston_runs(run, mem, terrains, full):
    """Phase 4b-5: every piston fixture segment and the piston race sweep of every race (main
    tick with all modelled enemies) on Mem class mem. Every piston frame drawn must be in the
    death set. Returns crc32 of the work RAM per run."""

    def check(m):
        got = pp.fixture_state(pp.tick_state(m.view(full)))
        for o in got[3:39]:
            if o[7] and o[7] not in run.death.ids:
                raise AssertionError(f"marble: piston frame {o[7]:05x} not in the death set")
        return got

    out = []
    for seg in pp.load_psfixture(run.trace(c.PISTON_FIXTURE, "pswrite")):
        m = me.setup(ms.fixture_mem(seg, terrains[seg["race"]], mem))
        for c6, c7, cam, frames, want in seg["ticks"]:
            ms.main_tick(m, c6 & 0xff, c7 & 0xff, cam & 0xffffffff, frames & 0xffffffff)
            mo.boxes_and_sort(m)
            if check(m) != want:
                raise AssertionError("marble: piston fixture replay differs, run marble_piston.py psfixture")
        out.append(zlib.crc32(bytes(m.ram)))
    for n in range(md.LEVELS):
        m = me.setup(mem(terrains[n], bytes(0x2000)))
        pp.sweep(m, n)
        check(m)
        out.append(zlib.crc32(bytes(m.ram)))
    return out


def sim_runs(run, mem, terrains, full):
    """Every run of every phase on Mem class mem; full: the ROM the check values read."""
    _physics_runs(run, mem, terrains)
    _physics_runs(run, mem, terrains, True)
    _object_runs(run, mem, terrains)
    _sprite_runs(run, mem, terrains)
    _death_runs(run, mem, terrains, full)
    _steelie_runs(run, mem, terrains, full)
    _slinky_runs(run, mem, terrains, full)
    _acid_runs(run, mem, terrains, full)
    _bird_runs(run, mem, terrains, full)
    _tube_runs(run, mem, terrains, full)
    _mini_runs(run, mem, terrains, full)
    _piston_runs(run, mem, terrains, full)
    _aerial_runs(run, mem, terrains, full)
