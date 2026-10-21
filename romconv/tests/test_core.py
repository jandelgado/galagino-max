import hashlib
import json

import pytest

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.core.manifest import Asset, Manifest
from romconv.registry import by_name, resolve


def _noop(ctx):
    pass


def _job(name="x", **kw):
    return Job(name=name, convert=_noop, **kw)


def _sha1(data):
    return hashlib.sha1(data).hexdigest()


def test_rom_reads_member_and_checks_sha1(paths, log, make_zip):
    data = b"\x01\x02\x03"
    make_zip("g.zip", {"a.bin": data})
    ctx = Context(_job(zips=("g.zip",)), paths, (), log)
    assert ctx.rom("a.bin", _sha1(data)) == data
    assert ctx.rom(["missing.bin", "a.bin"]) == data
    with pytest.raises(ConversionError) as err:
        ctx.rom("a.bin", _sha1(b"other"))
    assert "g.zip" in str(err.value)
    assert "a.bin" in str(err.value)
    with pytest.raises(ConversionError) as err:
        ctx.rom("nope.bin")
    assert "g.zip" in str(err.value)
    assert "nope.bin" in str(err.value)


def test_rom_optional_returns_none(paths, log, make_zip):
    make_zip("g.zip", {"a.bin": b"x"})
    ctx = Context(_job(zips=("g.zip",)), paths, (), log)
    assert ctx.rom_optional("nope.bin") is None
    assert ctx.rom_optional(["nope.bin", "a.bin"]) == b"x"


def test_header_written_on_clean_exit_only(paths, log):
    ctx = Context(_job(machine="m"), paths, (), log)
    with ctx.header("ok.h") as f:
        print("hello", file=f)
    assert (paths.machines / "m" / "ok.h").read_text() == "hello\n"
    with pytest.raises(RuntimeError):
        with ctx.header("bad.h") as f:
            print("half", file=f)
            raise RuntimeError("boom")
    assert not (paths.machines / "m" / "bad.h").exists()


def test_header_plan_and_remote_flags(paths, log):
    ctx = Context(_job("x", machine="m", plan=True), paths, (), log)
    f = ctx.header("a.h")
    assert (f.plan_game, f.remote_dir, f.machine) == ("x", "m", "m")
    assert f.manifest is ctx.manifest
    f = ctx.header("a.h", machine="namco54")
    assert (f.plan_game, f.remote_dir, f.machine) == ("x", None, "namco54")
    f = ctx.header("a.h", plan=False, remote=False)
    assert (f.plan_game, f.remote_dir) == (None, None)
    ctx = Context(_job("y", machine="m"), paths, (), log)
    assert ctx.header("a.h").plan_game is None
    assert ctx.path("b.bin", machine="z") == paths.machines / "z" / "b.bin"


def test_manifest_persist_layout(tmp_path):
    asset = Asset(dir="g", name="n", ctype="unsigned char", inner="", count=4, mode="PLAIN",
                  raw=b"\x01\x02\x03\x04", packed=b"FULL", lz64=b"L64", hint=None)
    manifest = Manifest()
    manifest.add(asset)
    assert manifest.dirs() == {"g"}
    assert manifest.assets("g") == [asset]
    assert manifest.assets("other") == []
    root = tmp_path / "assets"
    (root / "g").mkdir(parents=True)
    (root / "g" / "stale.raw").write_bytes(b"old")
    manifest.persist(root)
    assert sorted(p.name for p in (root / "g").iterdir()) == ["n.json", "n.lz", "n.lz64", "n.raw"]
    assert (root / "g" / "n.json").read_bytes() == json.dumps(
        {"ctype": "unsigned char", "inner": "", "count": 4, "mode": "PLAIN"}).encode()
    assert (root / "g" / "n.json").read_text() == (
        '{"ctype": "unsigned char", "inner": "", "count": 4, "mode": "PLAIN"}')
    assert (root / "g" / "n.raw").read_bytes() == b"\x01\x02\x03\x04"
    assert (root / "g" / "n.lz").read_bytes() == b"FULL"
    assert (root / "g" / "n.lz64").read_bytes() == b"L64"


def test_manifest_persist_without_lz64(tmp_path):
    manifest = Manifest()
    manifest.add(Asset(dir="g", name="n", ctype="unsigned char", inner="", count=1,
                       mode="COMPRESSED", raw=b"\x01", packed=b"P", lz64=None, hint=None))
    manifest.persist(tmp_path)
    assert sorted(p.name for p in (tmp_path / "g").iterdir()) == ["n.json", "n.lz", "n.raw"]


def test_resolve_adds_deps_and_phase2():
    a = _job("a", deps=("b",))
    b = _job("b", deps=("c",))
    c = _job("c")
    d = _job("d")
    last = _job("last", phase=2)
    jobs = (last, d, c, b, a)
    assert [j.name for j in resolve(["a"], jobs)] == ["last", "c", "b", "a"]
    assert [j.name for j in resolve(["d"], jobs)] == ["last", "d"]
    assert [j.name for j in resolve([], jobs)] == ["last", "d", "c", "b", "a"]


def test_unknown_name_lists_valid():
    jobs = (_job("a"), _job("b"))
    with pytest.raises(ConversionError) as err:
        by_name("zzz", jobs)
    assert "zzz" in str(err.value)
    assert "a" in str(err.value) and "b" in str(err.value)
    with pytest.raises(ConversionError):
        resolve(["zzz"], jobs)
