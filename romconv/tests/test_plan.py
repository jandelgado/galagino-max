import random
import time
from pathlib import Path

import pytest

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.core.manifest import Asset, Manifest
from romconv.core.paths import Paths, default
from romconv.plan.arena import (SCRATCH_BYTES, extras_key, parse_machine_header, plan_assets, plan_header, solve,
                                write_arena_plan)
from romconv.plan.layout import define_name, ini_path, min_blocks, min_capacity, read_layouts, remote_layouts
from romconv.plan.solver import ExhaustiveSolver, HighsSolver

W = [49152, 35040, 35040]  # Williams RAM + 2 scanout buffers
REMOTE, LOCAL = (69000, 110500), (118784, 110500)
LAYOUTS = {"esp32_local": LOCAL, "esp32_remote": REMOTE}


@pytest.fixture(params=[ExhaustiveSolver(), HighsSolver()], ids=["exhaustive", "highs"])
def solver(request):
    return request.param


def _a(name, raw, packed=None, align=1):
    """A planned asset that compresses to half unless said otherwise."""
    return (name, raw, raw // 2 if packed is None else packed, align)


def _noop(ctx):
    pass


# test_arena_layout


def test_layouts_from_platformio_ini():
    layouts = read_layouts(ini_path(default()))
    assert list(layouts) == ["esp32_local", "esp32_remote", "esp32s3_local", "rp2350", "rp2350_dvi"]
    assert layouts["esp32_local"] == (126976, 110500) and layouts["esp32_remote"] == REMOTE


@pytest.mark.parametrize("row", ["x = 1 2 3", "x = -1 2", "x = 0 2", "x = a b", "bad-name = 1 2"])
def test_bad_layout_row_rejected(tmp_path, row):
    # a bad row is an error naming it, not a silent default
    p = tmp_path / "platformio.ini"
    p.write_text(f"[arena]\n{row}\n")
    with pytest.raises(ValueError) as err:
        read_layouts(p)
    assert row.split(" = ")[0] in str(err.value)


def test_inline_comment_allowed(tmp_path):
    # PlatformIO strips "; ..." and "# ..." after a value; romconv must read the same row
    p = tmp_path / "platformio.ini"
    p.write_text("[arena]\nrp2350 = 118784 0 ; one pool\nx = 1 2 # two\n")
    assert read_layouts(p) == {"rp2350": (118784, 0), "x": (1, 2)}


def test_no_arena_section_rejected(tmp_path):
    p = tmp_path / "platformio.ini"
    p.write_text("[env]\nplatform = native\n")
    with pytest.raises(ValueError):
        read_layouts(p)


def test_min_blocks_and_capacity():
    # hand checks of unplanned games: the smallest block / pool among the rows the game builds for
    layouts = {"esp32_local": (118784, 110500), "esp32_remote": (69000, 110500), "rp2350": (100000, 90000)}
    assert min_blocks(layouts) == (69000, 90000)
    assert min_capacity(layouts, remote=True) == 179500
    assert min_capacity(layouts, remote=False) == 190000


def test_remote_layouts_and_define_name():
    assert remote_layouts({"esp32_local": (1, 1), "esp32_remote": (1, 1), "rp2350": (1, 0)}) == ["esp32_remote"]
    assert define_name("esp32_local") == "ARENA_LAYOUT_ESP32_LOCAL"


def test_b_zero_layout_never_assigns_b(solver):
    # one pool: A holds one asset (and the scratch gap), the other stays PLAIN, nothing lands in B
    m, r = solve([_a("a", 2000), _a("b", 2000)], [], (2000 + SCRATCH_BYTES, 0), solver=solver)
    assert sorted(m.values()) == ["A", "PLAIN"] and r == (2000, 0), (m, r)


# test_arena_plan


def test_defender_fits_by_placement(solver):
    m, r = solve([_a("d_main", 12288), _a("d_banked", 28672), _a("d_sound", 2048)], W, REMOTE,
                 any_order=True, solver=solver)
    assert "PLAIN" not in m.values(), m


def test_joust_main_rom_plain_remote_none_local(solver):
    j = [_a("j_main", 49152), _a("j_sound", 4096)]
    m, r = solve(j, W, REMOTE, any_order=True, solver=solver)
    assert [n for n, v in m.items() if v == "PLAIN"] == ["j_main"], m
    m, r = solve(j, W, LOCAL, any_order=True, solver=solver)
    assert "PLAIN" not in m.values(), m


def test_pins(solver):
    j = [_a("j_main", 49152), _a("j_sound", 4096)]
    m, r = solve([_a("a", 10)], [], REMOTE, flash=["a"], solver=solver)
    assert m["a"] == "PLAIN"
    with pytest.raises(ValueError):
        solve(j, W, REMOTE, ram=["j_main"], any_order=True, solver=solver)  # infeasible pin
    with pytest.raises(ValueError):
        solve([_a("a", 10)], [], REMOTE, ram=["nope"], solver=solver)  # unknown pin


def test_reservation_rounded_to_4(solver):
    m, r = solve([_a("x", 5)], [], REMOTE, solver=solver)
    assert r == (8, 0), r
    assert m["x"] == "A"


def test_oversize_asset_plain_not_error(solver):
    m, r = solve([_a("big", 205664, align=4), _a("s", 100)], [], REMOTE, solver=solver)
    assert m["big"] == "PLAIN" and m["s"] != "PLAIN"


def test_scratch_gap_required(solver):
    # two assets fill A and B exactly: one must stay PLAIN so a block keeps SCRATCH_BYTES
    m, r = solve([_a("x", 10000), _a("y", 10000)], [], (10000, 10000), solver=solver)
    assert sorted(m.values()) in (["A", "PLAIN"], ["B", "PLAIN"]), m
    m, r = solve([_a("x", 10000), _a("y", 10000)], [], (10000, 10000 + SCRATCH_BYTES), solver=solver)
    assert "PLAIN" not in m.values(), m


def test_extras_first_fit_rejects_optimum(solver):
    # both assets in one block: the 6000 B extra lands in the other and no block
    # keeps SCRATCH_BYTES (A 10000 = asset + extra, B 9232 = asset + scratch);
    # the nogood round splits them, PLAIN would cost 2000 B of flash
    m, r = solve([_a("x", 4000), _a("y", 4000)], [6000], (10000, 4000 + SCRATCH_BYTES), solver=solver)
    assert sorted(m.values()) == ["A", "B"], m


def test_plan_header():
    h = plan_header("1942", {"esp32_local": ({"x": "A"}, (8, 0)), "esp32_remote": ({"x": "PLAIN"}, (0, 0))},
                    {"x": "unsigned char[8][8]"}, 0, LAYOUTS)
    assert '#include "../../emulation/asset.h"' in h
    assert "using x_asset = Asset<unsigned char[8][8], PLAIN>;" in h
    assert "using x_asset = Asset<unsigned char[8][8], COMPRESSED>;" in h
    assert "#ifndef _1942_ARENA_PLAN_H" in h
    assert "static constexpr ArenaPlan _1942_arena_plan = { 0, 0 };" in h
    assert "static constexpr ArenaPlan _1942_arena_plan = { 8, 0 };" in h
    assert "#define x_PLAIN 1" in h and "#define x_BLOCK ARENA_ANY" in h
    assert "#define x_PLAIN 0" in h and "#define x_BLOCK ARENA_A" in h
    assert "Arena::BLOCK_A_SIZE == 69000 && Arena::BLOCK_B_SIZE == 110500" in h
    assert "Arena::BLOCK_A_SIZE == 118784 && Arena::BLOCK_B_SIZE == 110500" in h
    # one block per layout in ini order, a build with another layout fails
    assert (h.index("#if defined(ARENA_LAYOUT_ESP32_LOCAL)") < h.index("{ 8, 0 }")
            < h.index("#elif defined(ARENA_LAYOUT_ESP32_REMOTE)") < h.index("{ 0, 0 }")
            < h.index('#error "no Arena plan for this layout'))
    assert "ASSETS_REMOTE" not in h


def test_plan_header_duplicate_sizes():
    # placeholders share sizes: still one block per layout
    layouts = {"esp32_local": LOCAL, "esp32s3_local": LOCAL, "rp2350": (118784, 0)}
    plans = {name: ({"x": "A"}, (8, 0)) for name in layouts}
    h = plan_header("g", plans, {"x": "unsigned char"}, 0, layouts)
    assert h.count("#if defined(") == 1 and h.count("#elif defined(") == 2
    assert "#elif defined(ARENA_LAYOUT_RP2350)" in h
    assert h.count("Arena::BLOCK_A_SIZE == 118784 && Arena::BLOCK_B_SIZE == 110500") == 2


def test_extras_key():
    # the plan records ARENA_EXTRA and ARENA_EXTRA_ANY_ORDER it was solved for;
    # arena.h arenaExtrasKey() computes the same (test_arena)
    assert extras_key([49152, 35040], False) == 49152 * 31 + 35040
    assert extras_key([49152, 35040], True) == (49152 * 31 + 35040) ^ 0x80000000
    assert extras_key([35040, 49152], False) != extras_key([49152, 35040], False)
    assert extras_key([], False) == 0
    h = plan_header("g", {"esp32_local": ({}, (0, 0)), "esp32_remote": ({}, (0, 0))}, {}, extras_key([7], False),
                    LAYOUTS)
    assert "static constexpr uint32_t g_arena_extras_key = 7u;" in h


def test_machine_header_parse(tmp_path):
    p = tmp_path / "g.h"
    p.write_text("class g {\n  static constexpr uint32_t ARENA_EXTRA[] = { 49152, 35040,\n    35040 };  // hw\n"
                 '  static constexpr const char *ARENA_RAM[] = { "a", "b" };\n};\n')
    assert parse_machine_header(p) == ([49152, 35040, 35040], ["a", "b"], [], False)
    p.write_text("class g {\n};\n")
    assert parse_machine_header(p) == ([], [], [], False)
    p.write_text("  static constexpr bool ARENA_EXTRA_ANY_ORDER = true;\n")
    assert parse_machine_header(p) == ([], [], [], True)
    p.write_text("static constexpr uint32_t ARENA_EXTRA[] = { WilliamsBoard::RAM_SIZE };\n")
    with pytest.raises(ValueError):
        parse_machine_header(p)  # expression


def test_extras_order(solver):
    # extras run in declared order; any_order: every order must fit and leave
    # the scratch gap (Arena::alloc as function arguments: unspecified order).
    # Reversed, 60 lands in A and 100 in B: no block keeps the gap.
    solve([], [100, 60], (100, 60 + SCRATCH_BYTES), solver=solver)
    with pytest.raises(ValueError):
        solve([], [100, 60], (100, 60 + SCRATCH_BYTES), any_order=True, solver=solver)


def test_any_order_capped(solver):
    # every order is encoded: five distinct extras are 120 orders, past the budget
    with pytest.raises(ValueError, match="ARENA_EXTRA_ANY_ORDER"):
        solve([], [8, 16, 24, 32, 40], (1000, 1000), any_order=True, solver=solver)


def test_16_assets_solve_in_reasonable_time(solver):
    rnd = random.Random(1)
    assets = [_a(f"a{i}", rnd.choice([4096, 8192, 16384, 32768, 65536]) + rnd.randrange(0, 4096))
              for i in range(16)]
    t0 = time.time()
    solve(assets, [4096], REMOTE, solver=solver)
    assert time.time() - t0 < 5


# manifest based plan


# (name, raw bytes, packed bytes, align, type); packed 0: the fixture carries no LZMA stream
_MARKERS = [
    ("g_main", 16384, 0, 1, "unsigned char"),
    ("g_tiles", 8192, 0, 1, "unsigned char[8][8]"),
    ("g_cmap", 64, 0, 2, "unsigned short"),
    ("g_words", 4096, 0, 4, "uint32_t[2]"),
]


def _manifest_from_markers(markers):
    ctypes = ("unsigned char", "signed char", "unsigned short", "uint32_t")
    manifest = Manifest()
    for name, size, _packed, _align, type_ in markers:
        ctype = next(c for c in sorted(ctypes, key=len, reverse=True) if type_.startswith(c))
        manifest.add(Asset(dir="g", name=name, ctype=ctype, inner=type_[len(ctype):], count=size,
                           mode="PLANNED", raw=bytes(size), packed=b"", lz64=b""))
    return manifest


def test_plan_assets_ignore_unplanned():
    manifest = _manifest_from_markers(_MARKERS)
    manifest.add(Asset(dir="g", name="other", ctype="unsigned char", inner="", count=1, mode="COMPRESSED",
                       raw=b"\0", packed=b"", lz64=None))
    # the plan needs the LZMA size: a planned asset's flash cost when COMPRESSED
    assert plan_assets(manifest.assets()) == _MARKERS


def _plan_repo(tmp_path):
    """tmp repo with a two-layout platformio.ini and a machine header for game g."""
    paths = Paths(tmp_path)
    ini = ini_path(paths)
    ini.parent.mkdir(parents=True)
    ini.write_text(f"[arena]\nesp32_local = {LOCAL[0]} {LOCAL[1]}\nesp32_remote = {REMOTE[0]} {REMOTE[1]}\n")
    machine = paths.machines / "g"
    machine.mkdir(parents=True)
    (machine / "g.h").write_text("class g {\n  static constexpr uint32_t ARENA_EXTRA[] = { 49152, 35040 };\n"
                                 '  static constexpr const char *ARENA_FLASH[] = { "g_cmap" };\n};\n')
    return paths


# the same split in both layouts of the fixture ini: g fits each
_G_BLOCK = """\
#define g_cmap_PLAIN 1
#define g_cmap_BLOCK ARENA_ANY
using g_cmap_asset = Asset<unsigned short, PLAIN>;
#define g_main_PLAIN 0
#define g_main_BLOCK ARENA_A
using g_main_asset = Asset<unsigned char, COMPRESSED>;
#define g_tiles_PLAIN 0
#define g_tiles_BLOCK ARENA_A
using g_tiles_asset = Asset<unsigned char[8][8], COMPRESSED>;
#define g_words_PLAIN 0
#define g_words_BLOCK ARENA_A
using g_words_asset = Asset<uint32_t[2], COMPRESSED>;
static constexpr ArenaPlan g_arena_plan = { 28672, 0 };
"""

_PLAN_BODY = """\
#ifndef G_ARENA_PLAN_H
#define G_ARENA_PLAN_H
#include "../../emulation/asset.h"
static constexpr uint32_t g_arena_extras_key = 1558752u;
#if defined(ARENA_LAYOUT_ESP32_LOCAL)
static_assert(Arena::BLOCK_A_SIZE == 118784 && Arena::BLOCK_B_SIZE == 110500, "Arena layout changed: rerun romconv");
""" + _G_BLOCK + """\
#elif defined(ARENA_LAYOUT_ESP32_REMOTE)
static_assert(Arena::BLOCK_A_SIZE == 69000 && Arena::BLOCK_B_SIZE == 110500, "Arena layout changed: rerun romconv");
""" + _G_BLOCK + """\
#else
#error "no Arena plan for this layout: add it to [arena] in platformio.ini and rerun romconv"
#endif
#endif
"""


def test_plan_header_text(tmp_path, log):
    # asset order by name, remote and local split of the same assets
    paths = _plan_repo(tmp_path)
    ctx = Context(Job("g", _noop, machine="g", plan=True), paths, (), log)
    for a in _manifest_from_markers(_MARKERS).assets():
        ctx.manifest.add(a)
    write_arena_plan(ctx)
    text = (paths.machines / "g" / "g_arena_plan.h").read_text()
    assert text.split("\n", 1)[1] == _PLAN_BODY


def test_plan_without_planned_assets_is_error(tmp_path, log):
    paths = _plan_repo(tmp_path)
    ctx = Context(Job("g", _noop, machine="g", plan=True), paths, (), log)
    with pytest.raises(ConversionError) as err:
        write_arena_plan(ctx)
    assert "g:" in str(err.value)


def test_error_names_layout(tmp_path, log):
    # a split that fits no block is a ConversionError naming game and layout
    paths = _plan_repo(tmp_path)
    ctx = Context(Job("g", _noop, machine="g", plan=True), paths, (), log)
    for a in _manifest_from_markers([("g_big", 400000, 0, 1, "unsigned char")]).assets():
        ctx.manifest.add(a)
    (paths.machines / "g" / "g.h").write_text('static constexpr const char *ARENA_RAM[] = { "g_big" };\n')
    with pytest.raises(ConversionError, match=r"g esp32_local: does not fit layout 118784\+110500"):
        write_arena_plan(ctx)
