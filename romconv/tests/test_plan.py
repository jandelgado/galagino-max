import random
import shutil
import time
from pathlib import Path

import pytest

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.core.manifest import Asset, Manifest
from romconv.core.paths import Paths, default
from romconv.plan.arena import (extras_key, parse_machine_header, plan_assets, plan_header, solve,
                                write_arena_plan)
from romconv.plan.layout import define_name, ini_path, read_layouts, remote_layouts

W = [49152, 35040, 35040]  # Williams RAM + 2 scanout buffers
REMOTE, LOCAL = (69000, 110500), (118784, 110500)
LAYOUTS = {"esp32_local": LOCAL, "esp32_remote": REMOTE}


def _noop(ctx):
    pass


# test_arena_layout


def test_layouts_from_platformio_ini():
    layouts = read_layouts(ini_path(default()))
    assert list(layouts) == ["esp32_local", "esp32_remote", "esp32s3_local", "rp2350"]
    assert layouts["esp32_local"] == LOCAL and layouts["esp32_remote"] == REMOTE


@pytest.mark.parametrize("row", ["x = 1 2 3", "x = -1 2", "x = 0 2", "x = a b", "bad-name = 1 2"])
def test_bad_layout_row_rejected(tmp_path, row):
    # a bad row is an error naming it, not a silent default
    p = tmp_path / "platformio.ini"
    p.write_text(f"[arena]\n{row}\n")
    with pytest.raises(ValueError) as err:
        read_layouts(p)
    assert row.split(" = ")[0] in str(err.value)


def test_no_arena_section_rejected(tmp_path):
    p = tmp_path / "platformio.ini"
    p.write_text("[env]\nplatform = native\n")
    with pytest.raises(ValueError):
        read_layouts(p)


def test_remote_layouts_and_define_name():
    assert remote_layouts({"esp32_local": (1, 1), "esp32_remote": (1, 1), "rp2350": (1, 0)}) == ["esp32_remote"]
    assert define_name("esp32_local") == "ARENA_LAYOUT_ESP32_LOCAL"


def test_b_zero_layout_never_assigns_b():
    m, r = solve([("a", 1000, 1), ("b", 2000, 1)], [], (118784, 0))
    assert set(m.values()) <= {"A", "PLAIN"} and r[1] == 0


# test_arena_plan


def test_defender_fits_by_placement():
    m, r = solve([("d_main", 12288, 1), ("d_banked", 28672, 1), ("d_sound", 2048, 1)], W, REMOTE,
                 any_order=True)
    assert "PLAIN" not in m.values(), m


def test_joust_main_rom_plain_remote_none_local():
    j = [("j_main", 49152, 1), ("j_sound", 4096, 1)]
    m, r = solve(j, W, REMOTE, any_order=True)
    assert [n for n, v in m.items() if v == "PLAIN"] == ["j_main"], m
    m, r = solve(j, W, LOCAL, any_order=True)
    assert "PLAIN" not in m.values(), m


def test_pins():
    j = [("j_main", 49152, 1), ("j_sound", 4096, 1)]
    m, r = solve([("a", 10, 1)], [], REMOTE, flash=["a"])
    assert m["a"] == "PLAIN"
    with pytest.raises(ValueError):
        solve(j, W, REMOTE, ram=["j_main"], any_order=True)  # infeasible pin
    with pytest.raises(ValueError):
        solve([("a", 10, 1)], [], REMOTE, ram=["nope"])  # unknown pin


def test_reservation_rounded_to_4():
    m, r = solve([("x", 5, 1)], [], REMOTE)
    assert r in ((8, 0), (0, 8)), r
    assert m["x"] in ("A", "B")


def test_oversize_asset_plain_not_error():
    m, r = solve([("big", 205664, 4), ("s", 100, 1)], [], REMOTE)
    assert m["big"] == "PLAIN" and m["s"] != "PLAIN"


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


def test_extras_order():
    # extras run in declared order; any_order: every order must fit
    # (Arena::alloc as function arguments: unspecified order)
    solve([], [100, 60], (100, 60))
    with pytest.raises(ValueError):
        solve([], [100, 60], (100, 60), any_order=True)


def test_tie_break_by_converter_mode():
    # equal PLAIN bytes: the converter's mode (hint) wins over names
    a = [("a_tiles", 8192, 1, "COMPRESSED"), ("b_rom", 8192, 1, "PLAIN"), ("c", 60000, 1, "COMPRESSED")]
    m, r = solve(a, [], (60000, 8192))
    assert m["b_rom"] == "PLAIN" and m["a_tiles"] != "PLAIN", m


def test_converter_split_kept_when_it_fits():
    a = [("x", 100, 1, "PLAIN"), ("y", 50, 1, "COMPRESSED")]
    m, r = solve(a, [], (1000, 1000))
    assert m["x"] == "PLAIN" and m["y"] != "PLAIN", m
    m, r = solve(a, [], (40, 40))  # does not fit: fewest PLAIN bytes
    assert m["x"] == "PLAIN" and m["y"] == "PLAIN", m
    m, r = solve([("x", 100, 1, "COMPRESSED"), ("y", 50, 1, "COMPRESSED")], [], (100, 0))
    assert m["x"] == "A" and m["y"] == "PLAIN", m


def test_16_assets_solve_in_reasonable_time():
    rnd = random.Random(1)
    assets = [(f"a{i}", rnd.choice([4096, 8192, 16384, 32768, 65536]) + rnd.randrange(0, 4096), 1)
              for i in range(16)]
    t0 = time.time()
    solve(assets, [4096], REMOTE)
    assert time.time() - t0 < 5


# manifest based plan


_MARKERS = [
    ("g_main", 16384, 1, "COMPRESSED", "unsigned char"),
    ("g_tiles", 8192, 1, "PLAIN", "unsigned char[8][8]"),
    ("g_cmap", 64, 2, "COMPRESSED", "unsigned short"),
    ("g_words", 4096, 4, "COMPRESSED", "uint32_t[2]"),
]


def _manifest_from_markers(markers):
    ctypes = ("unsigned char", "signed char", "unsigned short", "uint32_t")
    manifest = Manifest()
    for name, size, _align, hint, type_ in markers:
        ctype = next(c for c in sorted(ctypes, key=len, reverse=True) if type_.startswith(c))
        manifest.add(Asset(dir="g", name=name, ctype=ctype, inner=type_[len(ctype):], count=size,
                           mode="PLANNED", raw=bytes(size), packed=b"", lz64=b"", hint=hint))
    return manifest


def test_plan_assets_ignore_unplanned():
    manifest = _manifest_from_markers(_MARKERS)
    manifest.add(Asset(dir="g", name="other", ctype="unsigned char", inner="", count=1, mode="COMPRESSED",
                       raw=b"\0", packed=b"", lz64=None, hint=None))
    assert plan_assets(manifest.assets()) == _MARKERS


def _plan_repo(tmp_path):
    """tmp repo with the real platformio.ini and a machine header for game g."""
    paths = Paths(tmp_path)
    ini = ini_path(paths)
    ini.parent.mkdir(parents=True)
    shutil.copy(ini_path(default()), ini)
    machine = paths.machines / "g"
    machine.mkdir(parents=True)
    (machine / "g.h").write_text("class g {\n  static constexpr uint32_t ARENA_EXTRA[] = { 49152, 35040 };\n"
                                 '  static constexpr const char *ARENA_FLASH[] = { "g_cmap" };\n};\n')
    return paths


# the same split in every layout of the real platformio.ini: g fits each
_G_BLOCK = """\
#define g_cmap_PLAIN 1
#define g_cmap_BLOCK ARENA_ANY
using g_cmap_asset = Asset<unsigned short, PLAIN>;
#define g_main_PLAIN 0
#define g_main_BLOCK ARENA_A
using g_main_asset = Asset<unsigned char, COMPRESSED>;
#define g_tiles_PLAIN 1
#define g_tiles_BLOCK ARENA_ANY
using g_tiles_asset = Asset<unsigned char[8][8], PLAIN>;
#define g_words_PLAIN 0
#define g_words_BLOCK ARENA_A
using g_words_asset = Asset<uint32_t[2], COMPRESSED>;
static constexpr ArenaPlan g_arena_plan = { 20480, 0 };
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
#elif defined(ARENA_LAYOUT_ESP32S3_LOCAL)
static_assert(Arena::BLOCK_A_SIZE == 118784 && Arena::BLOCK_B_SIZE == 110500, "Arena layout changed: rerun romconv");
""" + _G_BLOCK + """\
#elif defined(ARENA_LAYOUT_RP2350)
static_assert(Arena::BLOCK_A_SIZE == 118784 && Arena::BLOCK_B_SIZE == 110500, "Arena layout changed: rerun romconv");
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


def test_plan_that_does_not_fit_is_conversion_error(tmp_path, log):
    paths = _plan_repo(tmp_path)
    ctx = Context(Job("g", _noop, machine="g", plan=True), paths, (), log)
    for a in _manifest_from_markers([("g_big", 400000, 1, "COMPRESSED", "unsigned char")]).assets():
        ctx.manifest.add(a)
    (paths.machines / "g" / "g.h").write_text('static constexpr const char *ARENA_RAM[] = { "g_big" };\n')
    with pytest.raises(ConversionError):
        write_arena_plan(ctx)
