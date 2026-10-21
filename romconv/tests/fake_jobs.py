"""Importable jobs for the runner tests (workers find them by module name)."""

import os
import time

from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.core.manifest import Asset


def _write(ctx, text):
    with ctx.header(f"{ctx.job.name}.h") as f:
        print(text, file=f)


def _ok(ctx):
    ctx.log.info("log of %s", ctx.job.name)
    _write(ctx, "ok")


def _boom(ctx):
    ctx.log.info("log of boom")
    with ctx.header("boom.h") as f:
        print("half written", file=f)
        raise ConversionError("exploded")


def _slow(ctx):
    time.sleep(0.5)
    _write(ctx, "slow")


def _pid(ctx):
    _write(ctx, str(os.getpid()))


def _phase2(ctx):
    seen = sorted(p.name for p in ctx.path("x.h").parent.iterdir())
    _write(ctx, " ".join(seen))


def _asset(ctx):
    ctx.manifest.add(Asset(dir="shared", name=ctx.job.name, ctype="unsigned char", inner="",
                           count=1, mode="PLAIN", raw=b"\x00", packed=b"P", lz64=b"Q", hint=None))
    _write(ctx, "asset")


def _job(name, convert, **kw):
    return Job(name=name, convert=convert, machine="fake", **kw)


OK = (_job("ok_a", _ok), _job("ok_b", _ok), _job("slow", _slow))
BOOM = _job("boom", _boom)
PID = _job("pid", _pid)
PHASE2 = _job("late", _phase2, phase=2)
ASSET_A = _job("asset_a", _asset)
ASSET_B = _job("asset_b", _asset)

ALL = (*OK, BOOM, PID, PHASE2, ASSET_A, ASSET_B)
