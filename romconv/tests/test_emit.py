import importlib
import re
import struct
from pathlib import Path

import pytest

from romconv.core.context import Context
from romconv.core.header import HeaderWriter
from romconv.core.job import Job
from romconv.core.manifest import Manifest
from romconv.core.paths import Paths, default
from romconv.emit.asset import PLAIN_DICT_BYTES, emit_compressed, emit_plain
from romconv.emit.lzma import lzma_decompress
from romconv.remote.bundle import asset_id

LEGACY_DIR = Path(__file__).resolve().parents[1] / "internal" / "pyconv"


def _noop(ctx):
    pass


def _emit(tmp_path, fn, *args, machine="foo", plan_game=None, remote_dir=None, **kw):
    """Run an emit function on a HeaderWriter; (text, manifest)."""
    manifest = Manifest()
    f = HeaderWriter(tmp_path / machine / "x.h", machine, plan_game, remote_dir, manifest)
    with f:
        fn(f, *args, **kw)
    return f.path.read_text(), manifest


def _packed_bytes(out, name):
    m = re.search(name + r"_packed\[\] = \{(.*?)\};", out, re.S)
    return bytes(int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", m.group(1)))


def _packed_of(out, name):
    return lzma_decompress(_packed_bytes(out, name))


def _plain_values(out, name):
    m = re.search(name + r"_data\[\][^=]*= \{(.*?)\};", out, re.S)
    return [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", m.group(1))]


# test_asset_emit_logic


def test_compressed_byte_round_trip(tmp_path):
    values = list(range(256)) * 4  # 1024 unsigned char values
    out, _ = _emit(tmp_path, emit_compressed, "_test_rom", "unsigned char", "", len(values), values)
    assert _packed_of(out, "_test_rom") == bytes(v & 0xFF for v in values), "compressed round-trip mismatch"
    assert "static constexpr FlashAsset<unsigned char, COMPRESSED> _test_rom_blob = { _test_rom_packed, sizeof(_test_rom_packed), 1024 };" in out
    assert "static Asset<" not in out


def test_compressed_16bit_row_round_trip(tmp_path):
    values16 = [0x1234, 0xABCD, 0x0001, 0xFFFF]
    out, _ = _emit(tmp_path, emit_compressed, "_test_rows", "unsigned short", "[2]", 2, values16)
    expected = b"".join(struct.pack("<H", v) for v in values16)
    assert _packed_of(out, "_test_rows") == expected, "16-bit little-endian round-trip mismatch"
    assert "static constexpr FlashAsset<unsigned short[2], COMPRESSED> _test_rows_blob = { _test_rows_packed, sizeof(_test_rows_packed), 2 };" in out


def test_plain_wraps_body(tmp_path):
    out, _ = _emit(tmp_path, emit_plain, "_test_plain", "uint32_t", "[2]", 3, " { 0x1,0x2 },\n { 0x3,0x4 }")
    assert "static const uint32_t _test_plain_data[][2] = {" in out
    assert "static constexpr FlashAsset<uint32_t[2], PLAIN> _test_plain_blob = { _test_plain_data, 3 };" in out


def test_compressed_has_ratio_comment(tmp_path):
    out, _ = _emit(tmp_path, emit_compressed, "test_arr", "unsigned char", "", 4, [0, 0, 0, 0])
    assert "// test_arr: 4 -> " in out, "missing size/ratio comment"
    assert "bytes" in out and "%" in out


# test_asset_emit_plan


def _reference(tmp_path):
    plain, _ = _emit(tmp_path, emit_plain, "_r", "uint32_t", "[2]", 2, " { 0x1,0x2 },\n { 0x3,0x4 }")
    comp, _ = _emit(tmp_path, emit_compressed, "_r", "unsigned short", "", 3, [1, 2, 0xFFFF])
    return plain, comp


def test_plain_planned_both_forms(tmp_path):
    out, _ = _emit(tmp_path, emit_plain, "_p", "uint32_t", "[2]", 2, " { 0x1,0x2 },\n { 0x3,0x4 } // 99",
                   plan_game="tst")
    for s in ('#include "../../emulation/asset.h"', '#include "tst_arena_plan.h"',
              "// arena-asset _p 16 4 PLAIN uint32_t[2]", "#if _p_PLAIN", "#else", "#endif",
              "alignas(4) static const uint32_t _p_data[][2] = {",
              "static constexpr FlashAsset<uint32_t[2], PLAIN> _p_blob = { _p_data, 2 };",
              "static constexpr FlashAsset<uint32_t[2], COMPRESSED> _p_blob = { _p_packed, sizeof(_p_packed), 2, _p_BLOCK };",
              ):
        assert s in out, (s, out)
    assert "using _p_asset" not in out  # the alias lives in the plan header
    assert _packed_of(out, "_p") == struct.pack("<4L", 1, 2, 3, 4)


def test_wrong_value_count_rejected(tmp_path):
    with pytest.raises(ValueError):
        _emit(tmp_path, emit_plain, "_bad", "uint32_t", "[2]", 2, " { 0x1,0x2 },\n { 0x3 }", plan_game="tst")


def test_compressed_planned_plain_array_and_marker(tmp_path):
    out, _ = _emit(tmp_path, emit_compressed, "_c", "unsigned short", "", 3, [1, 2, 0xFFFF], plan_game="tst")
    assert "// arena-asset _c 6 2 COMPRESSED unsigned short" in out
    assert _plain_values(out, "_c") == [1, 2, 0xFFFF]
    assert _packed_of(out, "_c") == struct.pack("<3H", 1, 2, 0xFFFF)


def test_signed_body_values_keep_bit_pattern(tmp_path):
    out, _ = _emit(tmp_path, emit_plain, "_s", "signed char", "", 3, "  -1, 2, -128", plan_game="tst")
    assert _packed_of(out, "_s") == bytes([0xFF, 0x02, 0x80])


def test_without_plan_output_unchanged(tmp_path):
    ref_plain, ref_comp = _reference(tmp_path)
    out, _ = _emit(tmp_path, emit_plain, "_r", "uint32_t", "[2]", 2, " { 0x1,0x2 },\n { 0x3,0x4 }")
    assert out == ref_plain
    out, _ = _emit(tmp_path, emit_compressed, "_r", "unsigned short", "", 3, [1, 2, 0xFFFF])
    assert out == ref_comp


def test_plan_false_keeps_unplanned_form(tmp_path):
    ref_plain, ref_comp = _reference(tmp_path)
    out, _ = _emit(tmp_path, emit_plain, "_r", "uint32_t", "[2]", 2, " { 0x1,0x2 },\n { 0x3,0x4 }",
                   plan_game="tst", plan=False)
    assert out == ref_plain
    out, _ = _emit(tmp_path, emit_compressed, "_r", "unsigned short", "", 3, [1, 2, 0xFFFF],
                   plan_game="tst", plan=False)
    assert out == ref_comp


def test_adpcm_stays_unplanned_legacy(tmp_path, monkeypatch):
    # legacy emitter reads ROMCONV_PLAN; removed with adpcm.py in Task 16
    import io

    monkeypatch.syspath_prepend(str(LEGACY_DIR))
    monkeypatch.setenv("ROMCONV_PLAN", "tst")
    f = io.StringIO()
    importlib.import_module("adpcm").emit_adpcm(f, "_a", [0, 100, -100, 50])
    assert "arena-asset" not in f.getvalue()


# test_asset_emit_remote

_GUARD = "#if defined(ASSETS_REMOTE) && !defined(BUILTIN_FOO)"


def _asset(manifest, name):
    (a,) = [a for a in manifest.assets() if a.name == name]
    return a


def test_plain_remote_form_and_manifest(tmp_path):
    out, manifest = _emit(tmp_path, emit_plain, "foo_x", "unsigned char", "", 3, "  0x01,0x02,0x03",
                          remote_dir="foo")
    remote = "static constexpr FlashAsset<unsigned char, PLAIN> foo_x_blob = { nullptr, 3, 0x%08Xu };" % asset_id("foo_x")
    local = "static constexpr FlashAsset<unsigned char, PLAIN> foo_x_blob = { foo_x_data, 3 };"
    assert _GUARD in out and remote in out and local in out, out
    assert out.index(_GUARD) < out.index(remote) < out.index("#else") < out.index(local) < out.index("#endif")
    a = _asset(manifest, "foo_x")
    assert (a.dir, a.ctype, a.inner, a.count, a.mode) == ("foo", "unsigned char", "", 3, "PLAIN")
    assert a.raw == b"\x01\x02\x03"
    assert lzma_decompress(a.lz64, PLAIN_DICT_BYTES) == b"\x01\x02\x03"
    assert lzma_decompress(a.packed) == b"\x01\x02\x03"  # ratio report


def test_compressed_remote_form_and_manifest(tmp_path):
    values = list(range(200)) * 3
    out, manifest = _emit(tmp_path, emit_compressed, "foo_c", "unsigned char", "", len(values), values,
                          remote_dir="foo")
    packed = _packed_bytes(out, "foo_c")
    remote = ("static constexpr FlashAsset<unsigned char, COMPRESSED> foo_c_blob = "
              "{ nullptr, %d, %d, ARENA_ANY, 0x%08Xu };" % (len(packed), len(values), asset_id("foo_c")))
    assert remote in out, out
    a = _asset(manifest, "foo_c")
    assert a.packed == packed
    assert a.raw == bytes(values)
    assert a.lz64 is None
    assert a.mode == "COMPRESSED"


def test_planned_remote_form_and_manifest(tmp_path):
    out, manifest = _emit(tmp_path, emit_compressed, "foo_p", "unsigned short", "", 3, [1, 2, 0xFFFF],
                          plan_game="foo", remote_dir="foo")
    packed = _packed_bytes(out, "foo_p")
    r_plain = "static constexpr FlashAsset<unsigned short, PLAIN> foo_p_blob = { nullptr, 3, 0x%08Xu };" % asset_id("foo_p")
    r_comp = ("static constexpr FlashAsset<unsigned short, COMPRESSED> foo_p_blob = "
              "{ nullptr, %d, 3, foo_p_BLOCK, 0x%08Xu };" % (len(packed), asset_id("foo_p")))
    assert r_plain in out and r_comp in out, out
    assert out.index(_GUARD) < out.index(r_plain) < out.index(r_comp) < out.index("alignas(4)")
    a = _asset(manifest, "foo_p")
    assert a.mode == "PLANNED"
    assert a.packed == packed
    assert lzma_decompress(a.lz64, PLAIN_DICT_BYTES) == bytes([1, 0, 2, 0, 0xFF, 0xFF])


def test_planned_asset_in_manifest(tmp_path):
    out, manifest = _emit(tmp_path, emit_plain, "foo_q", "unsigned char", "", 2, "  0x01,0x02",
                          plan_game="foo", remote_dir="foo")
    a = _asset(manifest, "foo_q")
    assert a.mode == "PLANNED"
    assert a.hint == "PLAIN"
    assert a.lz64 is not None
    assert "// arena-asset foo_q 2 1 PLAIN unsigned char" in out
    _, manifest = _emit(tmp_path, emit_compressed, "foo_r", "unsigned char", "", 2, [1, 2],
                        plan_game="foo", remote_dir="foo")
    assert _asset(manifest, "foo_r").hint == "COMPRESSED"


def test_no_remote_form_without_remote_dir(tmp_path):
    out, manifest = _emit(tmp_path, emit_compressed, "foo_logo", "unsigned char", "", 2, [1, 2],
                          remote_dir="foo", remote=False)
    assert "ASSETS_REMOTE" not in out and manifest.assets() == []


def test_shared_dir_no_remote_form(tmp_path, log):
    paths = Paths(tmp_path)
    ctx = Context(Job("williams", _noop, machine="williams"), paths, (), log)
    with ctx.header("x.h") as f:
        emit_compressed(f, "williams_rom", "unsigned char", "", 2, [1, 2])
    assert "ASSETS_REMOTE" not in (paths.machines / "williams" / "x.h").read_text()
    assert ctx.manifest.assets() == []


# test_blob_access


def test_no_direct_flash_asset_reads_in_machines():
    """Machine code must reach asset data through Asset (asset.h), never through
    a FlashAsset's own fields: in a mode 3 build a remote blob has data/packed
    == nullptr, only Asset resolves its id in the mounted bundle (zaxxon
    samples and marble tables crashed or would have crashed this way)."""
    machines = default().machines
    # <name>_blob.data / .packed, and a FlashAsset parameter p read as p.data / p.packed
    blob_field = re.compile(r"\b\w+_blob\s*\.\s*(data|packed)\b")
    param = re.compile(r"FlashAsset<[^>]*>\s*\*?\s*&?\s*(\w+)\s*[,)]")
    hits = []
    for path in sorted(machines.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        text = path.read_text(errors="replace")
        if "Generated by romconv" in text[:300]:
            continue  # the blobs themselves
        for m in blob_field.finditer(text):
            hits.append(f"{path}: {m.group(0)}")
        for m in param.finditer(text):
            body = text[m.end():m.end() + 400]
            for f in re.finditer(r"\b%s\s*(\.|->)\s*(data|packed)\b" % re.escape(m.group(1)), body):
                hits.append(f"{path}: {m.group(1)}{f.group(1)}{f.group(2)}")
    assert not hits, "FlashAsset fields read directly, wrap in Asset:\n" + "\n".join(sorted(set(hits)))


# legacy comparison, removed with internal/pyconv/asset_emit.py in Task 16


def _legacy_emit(tmp_path, monkeypatch, dirname, fn_name, *args, plan=None, **kw):
    monkeypatch.syspath_prepend(str(LEGACY_DIR))
    monkeypatch.setenv("ROMCONV_ASSET_DIR", str(tmp_path / "legacy_assets"))
    if plan:
        monkeypatch.setenv("ROMCONV_PLAN", plan)
    else:
        monkeypatch.delenv("ROMCONV_PLAN", raising=False)
    legacy = importlib.import_module("asset_emit")
    path = tmp_path / "legacy" / "machines" / dirname / "x.h"
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w") as f:
        getattr(legacy, fn_name)(f, *args, **kw)
    return path.read_text()


@pytest.mark.parametrize("plan", [None, "foo"])
def test_compressed_text_matches_legacy(tmp_path, monkeypatch, plan):
    values = list(range(200)) * 3
    args = ("foo_c", "unsigned short", "[2]", len(values) // 2, values)
    old = _legacy_emit(tmp_path, monkeypatch, "foo", "emit_compressed", *args, plan=plan)
    new, manifest = _emit(tmp_path, emit_compressed, *args, plan_game=plan, remote_dir="foo")
    assert new == old
    manifest.persist(tmp_path / "new_assets")
    _assert_same_sidecars(tmp_path)


@pytest.mark.parametrize("plan", [None, "foo"])
def test_plain_text_matches_legacy(tmp_path, monkeypatch, plan):
    body = " { 0x1,0x2 },\n { 0x3,0x4 }"
    args = ("foo_p", "uint32_t", "[2]", 2, body)
    old = _legacy_emit(tmp_path, monkeypatch, "foo", "emit_plain", *args, plan=plan)
    new, manifest = _emit(tmp_path, emit_plain, *args, plan_game=plan, remote_dir="foo")
    assert new == old
    manifest.persist(tmp_path / "new_assets")
    _assert_same_sidecars(tmp_path)


def _assert_same_sidecars(tmp_path):
    old_root, new_root = tmp_path / "legacy_assets", tmp_path / "new_assets"
    old = {p.relative_to(old_root): p.read_bytes() for p in old_root.rglob("*") if p.is_file()}
    new = {p.relative_to(new_root): p.read_bytes() for p in new_root.rglob("*") if p.is_file()}
    assert old and old == new
