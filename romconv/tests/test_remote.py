import hashlib
import json
import lzma
import re
import struct

import pytest

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.core.paths import default
from romconv.emit.asset import PLAIN_DICT_BYTES
from romconv.emit.lzma import lzma_compress
from romconv.registry import resolve
from romconv.remote import bundles, catalog
from romconv.remote.bundle import (CACHE_BYTES, FLAG_LZMA, FLAG_PACKED, TAG_BYTES, BundleWriter, asset_id,
                                   bundle_hash)
from romconv.emit.text import c_symbol
from romconv.remote.catalog import BASES, EXCLUDED, bundle_dirs, catalog_h, read_games


def _noop(ctx):
    pass


def _lz(raw, dict_size):
    filters = [{"id": lzma.FILTER_LZMA1, "dict_size": dict_size, "lc": 0, "lp": 0, "pb": 0}]
    return bytes([0]) + lzma.compress(raw, format=lzma.FORMAT_RAW, filters=filters)


# test_bundle


def test_asset_id_fnv1a_vectors():
    assert asset_id("") == 0x811C9DC5
    assert asset_id("a") == 0xE40C292C


def test_wire_header_entries_alignment():
    # wire bundle: header v2, PLAIN entries LZMA (flag 2), COMPRESSED packed (flag 1)
    plain_raw = b"\x01\x02\x03" * 100
    plain_lz = _lz(plain_raw, 65536)
    comp_raw = b"\x07" * 40
    comp_lz = _lz(comp_raw, 1 << 20)
    w = BundleWriter()
    w.add("_a", plain_raw, plain_lz, "PLAIN")
    w.add("_b", comp_raw, comp_lz, "COMPRESSED")
    data = w.build()
    assert data[:8] == struct.pack("<LHH", 0x42474C47, 2, 2)
    e0 = struct.unpack_from("<5L", data, 8)
    e1 = struct.unpack_from("<5L", data, 28)
    assert e0[0] == asset_id("_a") and e0[2:] == (len(plain_lz), len(plain_raw), 2), e0
    assert e1[0] == asset_id("_b") and e1[2:] == (len(comp_lz), len(comp_raw), 1), e1
    assert e0[1] == 48 and e1[1] % 4 == 0 and e1[1] >= e0[1] + e0[2], (e0, e1)
    assert data[e0[1]:e0[1] + e0[2]] == plain_lz
    assert data[e1[1]:e1[1] + e1[2]] == comp_lz
    assert len(data) == e1[1] + e1[2]

    # cache entry: PLAIN stored raw (flag 0), COMPRESSED copied (flag 1)
    entry = w.entry()
    assert entry[:8] == struct.pack("<LHH", 0x42474C47, 2, 2)
    c0 = struct.unpack_from("<5L", entry, 8)
    c1 = struct.unpack_from("<5L", entry, 28)
    assert c0 == (asset_id("_a"), 48, len(plain_raw), len(plain_raw), 0), c0
    assert c1[0] == asset_id("_b") and c1[2:] == (len(comp_lz), len(comp_raw), 1), c1
    assert c1[1] % 4 == 0 and c1[1] >= c0[1] + c0[2]
    assert entry[c0[1]:c0[1] + c0[2]] == plain_raw
    assert entry[c1[1]:c1[1] + c1[2]] == comp_lz
    assert len(entry) == c1[1] + c1[2]

    # bundle hash = first 8 bytes of SHA-256
    assert bundle_hash(data) == hashlib.sha256(data).digest()[:8]


def test_duplicate_id_rejected():
    # duplicate name (or FNV collision): the device would resolve the wrong asset
    w = BundleWriter()
    w.add("_a", b"\x01", _lz(b"\x01", 4096), "PLAIN")
    with pytest.raises(ValueError):
        w.add("_a", b"\x02", _lz(b"\x02", 4096), "PLAIN")


def test_64_entries_max():
    w = BundleWriter()
    for i in range(64):
        w.add(f"_e{i}", b"\x01", _lz(b"\x01", 4096), "PLAIN")
    with pytest.raises(ValueError):
        w.add("_e64", b"\x01", _lz(b"\x01", 4096), "PLAIN")


def test_oversize_entry_rejected():
    # entry + tag sector must fit the cache partition
    big = bytes(CACHE_BYTES - TAG_BYTES)
    w = BundleWriter()
    w.add("_big", big, _lz(big, 65536), "PLAIN")
    with pytest.raises(ValueError):
        w.entry()


def test_unknown_mode_rejected():
    with pytest.raises(ValueError):
        BundleWriter().add("_x", b"\x01", b"\x00", "RAW")


# test_bundles (temp sidecar tree: base + subclass dirs, a planned asset whose
# remote mode comes from the arena plan header)


def _ctx(paths, log, requested=()):
    return Context(Job("bundles", _noop, phase=2), paths, tuple(requested), log)


def _sidecar(paths, dirname, name, raw, mode):
    out = paths.assets / dirname
    out.mkdir(parents=True, exist_ok=True)
    meta = {"ctype": "unsigned char", "inner": "", "count": len(raw), "mode": mode}
    files = {".json": json.dumps(meta).encode(), ".raw": raw, ".lz": lzma_compress(raw)}
    if mode != "COMPRESSED":
        files[".lz64"] = lzma_compress(raw, PLAIN_DICT_BYTES)
    for ext, data in files.items():
        (out / (name + ext)).write_bytes(data)


@pytest.fixture
def tree(paths):
    # pacman: one COMPRESSED, one planned (remote layout PLAIN); mspacman: one PLAIN
    _sidecar(paths, "pacman", "pacman_rom", bytes(range(256)) * 8, "COMPRESSED")
    _sidecar(paths, "pacman", "pacman_tiles", bytes(70000), "PLANNED")
    _sidecar(paths, "mspacman", "mspacman_rom", b"\x05" * 300, "PLAIN")
    (paths.machines / "pacman").mkdir(parents=True)
    (paths.machines / "mspacman").mkdir(parents=True)
    (paths.machines / "pacman" / "pacman_arena_plan.h").write_text(
        "#ifdef ASSETS_REMOTE\n#define pacman_tiles_PLAIN 1\n#else\n#define pacman_tiles_PLAIN 0\n#endif\n")
    return paths


def test_bundle_contents_and_flags(tree, log):
    info = bundles.make_bundle(_ctx(tree, log), "mspacman")
    wire = (tree.server / "g" / info["hex"]).read_bytes()
    assert info["hex"] == bundle_hash(wire).hex()
    n = struct.unpack_from("<H", wire, 6)[0]
    entries = {struct.unpack_from("<5L", wire, 8 + 20 * i)[0]: struct.unpack_from("<5L", wire, 8 + 20 * i)
               for i in range(n)}
    assert set(entries) == {asset_id("mspacman_rom"), asset_id("pacman_rom"), asset_id("pacman_tiles")}
    assert entries[asset_id("mspacman_rom")][4] == FLAG_LZMA
    assert entries[asset_id("pacman_tiles")][4] == FLAG_LZMA
    assert entries[asset_id("pacman_rom")][4] == FLAG_PACKED


def test_bundle_header(tree, log):
    # BundleRef with hash, wire and entry length, name; built in -> nullptr
    info = bundles.make_bundle(_ctx(tree, log), "mspacman")
    wire = (tree.server / "g" / info["hex"]).read_bytes()
    h = (tree.machines / "mspacman" / "mspacman_bundle.h").read_text()
    hash_c = ", ".join("0x%02x" % b for b in bundle_hash(wire))
    ref = ("static constexpr BundleRef mspacman_bundle = { { %s }, %du, %du, \"MSPACMAN\" };"
           % (hash_c, len(wire), info["entry"]))
    # ref only if some dir of the class hierarchy is remote: mspacman built in
    # with pacman's assets, or pacman built in for an ENABLE_MSPACMAN-only env
    guard = "#if !defined(BUILTIN_MSPACMAN) || !defined(BUILTIN_PACMAN)"
    assert '#include "../../net/bundle.h"' in h and guard in h, h
    assert ref in h, h
    assert "#define mspacman_bundle_ref (&mspacman_bundle)" in h
    assert "#define mspacman_bundle_ref nullptr" in h
    assert f"// GET /g/{info['hex']}" in h


def test_digit_led_symbol(tree, log):
    _sidecar(tree, "1943", "_1943_rom", b"\x01" * 10, "PLAIN")
    bundles.make_bundle(_ctx(tree, log), "1943")
    h = (tree.machines / "1943" / "1943_bundle.h").read_text()
    assert "static constexpr BundleRef _1943_bundle = " in h and "#define _1943_bundle_ref" in h
    assert "#if !defined(BUILTIN_1943)" in h


def test_oversize_entry_fails(tree, log):
    _sidecar(tree, "gng", "gng_huge", bytes(0x100000), "PLAIN")
    with pytest.raises(ValueError):
        bundles.make_bundle(_ctx(tree, log), "gng")


def test_game_without_asset_blobs_has_no_bundle(tree, log):
    # hero: tables only
    info = bundles.make_bundle(_ctx(tree, log), "hero")
    h = (tree.machines / "hero" / "hero_bundle.h").read_text()
    assert info is None and "#define hero_bundle_ref nullptr" in h and "BundleRef" not in h, h


# bundles job


def _games_tree(paths, names=("aaa", "bbb")):
    (paths.src).mkdir(parents=True, exist_ok=True)
    (paths.src / "machines.h").write_text("".join(f"#ifdef ENABLE_{n.upper()}\n#endif\n" for n in names))
    for n in names:
        _sidecar(paths, n, f"{n}_rom", b"\x05" * 300, "PLAIN")
        (paths.machines / n).mkdir(parents=True, exist_ok=True)


def test_partial_run_bundles_only_requested(paths, log):
    _games_tree(paths)
    (paths.machines / "bbb" / "bbb_bundle.h").write_text("untouched\n")
    bundles.JOB.convert(_ctx(paths, log, ("aaa", "catalog")))
    assert (paths.machines / "aaa" / "aaa_bundle.h").exists()
    assert (paths.machines / "bbb" / "bbb_bundle.h").read_text() == "untouched\n"
    assert len(list((paths.server / "g").iterdir())) == 1


def test_full_run_bundles_every_catalog_game(paths, log):
    _games_tree(paths)
    bundles.JOB.convert(_ctx(paths, log, ("aaa", "bbb", "catalog", "bundles")))
    assert (paths.machines / "aaa" / "aaa_bundle.h").exists()
    assert (paths.machines / "bbb" / "bbb_bundle.h").exists()
    assert len(list((paths.server / "g").iterdir())) == 2


def test_requested_job_name_selects_game(paths, log):
    # burgertime is converted by the job named btime
    _games_tree(paths, ("burgertime",))
    bundles.JOB.convert(_ctx(paths, log, ("btime",)))
    assert (paths.machines / "burgertime" / "burgertime_bundle.h").exists()


def test_missing_sidecars_is_error(paths, log):
    _games_tree(paths)
    (paths.assets / "bbb" / "bbb_rom.json").unlink()
    for f in list((paths.assets / "bbb").iterdir()):
        f.unlink()
    (paths.assets / "bbb").rmdir()
    with pytest.raises(ConversionError) as err:
        bundles.JOB.convert(_ctx(paths, log, ("bbb",)))
    assert "bbb" in str(err.value)
    assert not (paths.machines / "bbb" / "bbb_bundle.h").exists()


def test_bundles_job_follows_catalog():
    assert bundles.JOB.phase == 2 and bundles.JOB.deps == ("catalog",)
    assert catalog.JOB.phase == 2


def test_galaga_pulls_namco54():
    jobs = (Job("namco54", _noop), Job("galaga", _noop, deps=("namco54",)), Job("pacman", _noop),
            catalog.JOB, bundles.JOB)
    assert [j.name for j in resolve(["galaga"], jobs)] == ["namco54", "galaga", "catalog", "bundles"]


def test_resolve_pulls_deps_of_always_jobs():
    jobs = (Job("owner", _noop), Job("util", _noop, deps=("owner",), always=True), Job("pacman", _noop),
            Job("late", _noop, deps=("owner",), phase=2))
    assert [j.name for j in resolve(["pacman"], jobs)] == ["owner", "util", "pacman", "late"]
    assert [j.name for j in resolve(["util"], jobs)] == ["owner", "util", "late"]


# test_catalog


def test_catalog_games():
    games = read_games(default())
    assert len(games) >= 118, len(games)
    assert "motorace" in games and not games["motorace"]
    assert {g for g, in_catalog in games.items() if in_catalog} == set(games) - EXCLUDED
    assert "1943" in games and games["1943"] and games["gng"]
    assert all(b in games for bases in BASES.values() for b in bases)


def test_bundle_dirs():
    # own dir, then base dirs whose assets the class constructs
    assert bundle_dirs("amidar") == ("amidar", "turtles", "scramble")
    assert bundle_dirs("mspacman") == ("mspacman", "pacman")
    assert bundle_dirs("gng") == ("gng",)


def test_catalog_h_text():
    # GAME_ compiled in, BUILTIN_ assets in the app
    text = catalog_h(read_games(default()))
    assert "#if defined(ENABLE_GNG) || defined(ASSETS_REMOTE)\n#define GAME_GNG\n#endif" in text
    assert "#if defined(ENABLE_MOTORACE)\n#define GAME_MOTORACE\n#endif" in text
    m = re.search(r"#if (.*)\n#define BUILTIN_PACMAN\n", text)
    for g in ("pacman", "mspacman", "pengo", "alibaba", "crush", "eyes", "lizwiz", "mrtnt", "theglob"):
        assert f"defined(ENABLE_{g.upper()})" in m.group(1), (g, m.group(1))
    assert "#if defined(ENABLE_GNG)\n#define BUILTIN_GNG\n#endif" in text
    m = re.search(r"#if (.*)\n#define BUILTIN_SCRAMBLE\n", text)
    assert "defined(ENABLE_AMIDAR)" in m.group(1) and "defined(ENABLE_SUPERCOBRA)" in m.group(1)
    assert "#define BUILTIN_NAMCO54\n" in text and "#define BUILTIN_WILLIAMS\n" in text


def test_catalog_job_writes_catalog_h(paths, log):
    paths.src.mkdir(parents=True)
    (paths.src / "machines.h").write_text("#ifdef ENABLE_AAA\n#endif\n#ifdef GAME_BBB\n#endif\n")
    catalog.JOB.convert(Context(catalog.JOB, paths, (), log))
    assert (paths.src / "catalog.h").read_text() == catalog_h({"aaa": True, "bbb": True})


def test_c_symbol():
    assert c_symbol("1943") == "_1943"
    assert c_symbol("galaga") == "galaga"
