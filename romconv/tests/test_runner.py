import multiprocessing
import os

import pytest

from romconv.core import runner
from romconv.core.errors import ConversionError
from tests import fake_full, fake_jobs


def _read(paths, name):
    return (paths.machines / "fake" / name).read_text()


def test_failure_other_jobs_finish_exit_1(paths, capsys):
    code = runner.run(["boom", "ok_a", "ok_b", "slow"], 2, paths, fake_jobs,
                      multiprocessing.get_context("spawn"))
    out = capsys.readouterr().out
    assert code == 1
    for name in ("ok_a.h", "ok_b.h", "slow.h"):
        assert (paths.machines / "fake" / name).exists()
    assert not (paths.machines / "fake" / "boom.h").exists()
    assert "log of boom" in out
    assert "log of ok_a" in out
    assert "exploded" in out
    assert "boom" in out.splitlines()[-1]
    # phase 2 does not run after a phase 1 failure
    assert not (paths.machines / "fake" / "late.h").exists()


def test_failure_in_process(paths, capsys):
    code = runner.run(["boom", "ok_a"], 1, paths, fake_jobs)
    assert code == 1
    assert (paths.machines / "fake" / "ok_a.h").exists()
    assert not (paths.machines / "fake" / "boom.h").exists()
    assert "log of boom" in capsys.readouterr().out


def test_j1_runs_in_process(paths):
    assert runner.run(["pid"], 1, paths, fake_jobs) == 0
    assert _read(paths, "pid.h").strip() == str(os.getpid())


def test_pool_spawn(paths):
    code = runner.run(["pid", "ok_a", "ok_b"], 2, paths, fake_jobs,
                      multiprocessing.get_context("spawn"))
    assert code == 0
    assert _read(paths, "pid.h").strip() != str(os.getpid())
    assert _read(paths, "ok_a.h") == "ok\n"


def test_phase2_after_phase1(paths):
    assert runner.run(["ok_a", "ok_b", "slow"], 3, paths, fake_jobs,
                      multiprocessing.get_context("spawn")) == 0
    assert _read(paths, "late.h").split() == ["ok_a.h", "ok_b.h", "slow.h"]


def test_manifest_dir_collision(paths):
    with pytest.raises(ConversionError) as err:
        runner.run(["asset_a", "asset_b"], 1, paths, fake_jobs)
    assert "shared" in str(err.value)
    assert "asset_a" in str(err.value) and "asset_b" in str(err.value)


def test_manifest_persisted(paths):
    assert runner.run(["asset_a"], 1, paths, fake_jobs) == 0
    assert (paths.assets / "shared" / "asset_a.json").exists()


def test_requested_defaults_to_names_or_all_jobs(paths):
    assert runner.run(["ok_a"], 1, paths, fake_jobs) == 0
    assert _read(paths, "seen.h").strip() == "ok_a"
    assert runner.run([], 1, paths, fake_full) == 0
    assert _read(paths, "seen.h").strip() == "ok_a,ok_b,slow,seen"
    assert runner.run(["ok_a"], 1, paths, fake_jobs, requested=("x", "y")) == 0
    assert _read(paths, "seen.h").strip() == "x,y"
