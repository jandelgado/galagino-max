import importlib
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
from romconv.plan.layout import header_path, read_layouts

LEGACY_DIR = Path(__file__).resolve().parents[1] / "internal" / "pyconv"

W = [49152, 35040, 35040]  # Williams RAM + 2 scanout buffers
REMOTE, LOCAL = (69000, 110500), (91000, 110500)
LAYOUTS = {"local": LOCAL, "remote": REMOTE}


def _noop(ctx):
    pass


# test_arena_layout


def test_layouts_from_arena_layout_h():
    layouts = read_layouts(header_path(default()))
    assert layouts["local"] == (91000, 110500) and layouts["remote"] == (69000, 110500)
    assert layouts == {"local": LOCAL, "remote": REMOTE}


def test_layout_missing_define_rejected(tmp_path):
    # a header missing a define is an error, not a silent default
    p = tmp_path / "h.h"
    p.write_text("#define ARENA_A_LOCAL 91000\n#define ARENA_B_SIZE 110500\n")
    with pytest.raises(ValueError):
        read_layouts(p)


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
    h = plan_header("1942", {"local": ({"x": "A"}, (8, 0)), "remote": ({"x": "PLAIN"}, (0, 0))},
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
    assert "Arena::BLOCK_A_SIZE == 91000 && Arena::BLOCK_B_SIZE == 110500" in h
    assert h.index("#ifdef ASSETS_REMOTE") < h.index("{ 0, 0 }") < h.index("#else") < h.index("{ 8, 0 }")


def test_extras_key():
    # the plan records ARENA_EXTRA and ARENA_EXTRA_ANY_ORDER it was solved for;
    # arena.h arenaExtrasKey() computes the same (test_arena)
    assert extras_key([49152, 35040], False) == 49152 * 31 + 35040
    assert extras_key([49152, 35040], True) == (49152 * 31 + 35040) ^ 0x80000000
    assert extras_key([35040, 49152], False) != extras_key([49152, 35040], False)
    assert extras_key([], False) == 0
    h = plan_header("g", {"local": ({}, (0, 0)), "remote": ({}, (0, 0))}, {}, extras_key([7], False), LAYOUTS)
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


def test_markers_legacy(tmp_path, monkeypatch):
    # parse_markers is replaced by the manifest; removed with arena_plan.py in Task 16
    monkeypatch.syspath_prepend(str(LEGACY_DIR))
    legacy = importlib.import_module("arena_plan")
    (tmp_path / "a.h").write_text("// arena-asset g_rom 16384 1 PLAIN unsigned char[8][8]\n#if g_rom_PLAIN\n")
    (tmp_path / "g_arena_plan.h").write_text("// arena-asset stale 1 1\n")
    assert legacy.parse_markers(str(tmp_path)) == [("g_rom", 16384, 1, "PLAIN", "unsigned char[8][8]")]


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
    """tmp repo with the real arena_layout.h and a machine header for game g."""
    paths = Paths(tmp_path)
    layout = header_path(paths)
    layout.parent.mkdir(parents=True)
    shutil.copy(header_path(default()), layout)
    machine = paths.machines / "g"
    machine.mkdir(parents=True)
    (machine / "g.h").write_text("class g {\n  static constexpr uint32_t ARENA_EXTRA[] = { 49152, 35040 };\n"
                                 '  static constexpr const char *ARENA_FLASH[] = { "g_cmap" };\n};\n')
    return paths


def test_plan_from_manifest_equals_marker_plan(tmp_path, log, monkeypatch):
    # the old path read `// arena-asset` markers back from the headers
    monkeypatch.syspath_prepend(str(LEGACY_DIR))
    legacy = importlib.import_module("arena_plan")
    paths = _plan_repo(tmp_path)
    machine = paths.machines / "g"
    (machine / "g_rom.h").write_text("".join(f"// arena-asset {n} {s} {a} {h} {t}\n"
                                              for n, s, a, h, t in _MARKERS))
    monkeypatch.setattr(legacy, "MACHINES", str(paths.machines))
    legacy.main("g")
    old = (machine / "g_arena_plan.h").read_text()
    (machine / "g_arena_plan.h").unlink()

    ctx = Context(Job("g", _noop, machine="g", plan=True), paths, (), log)
    for a in _manifest_from_markers(_MARKERS).assets():
        ctx.manifest.add(a)
    write_arena_plan(ctx)
    assert (machine / "g_arena_plan.h").read_text() == old


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
