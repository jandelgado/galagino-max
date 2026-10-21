import pytest

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.paths import Paths, default
from romconv.games import marble
from romconv.games.marble import convert as marble_convert
from romconv.games.marble import data as md
from romconv.games.marble import sid as msid
from romconv.games.marble.sim import marble_sim as ms
from romconv.registry import by_name, resolve


def test_job_shape():
    job = by_name("marble")
    assert job is marble.JOB
    assert job.machine == "marble" and job.plan and job.zips == ("marble.zip", "atarisy1.zip")


def test_every_level_has_a_name_and_bank():
    assert len(md.NAMES) == len(md.BANK) == md.LEVELS


def test_sound_notes_are_package_data():
    notes = msid.load_notes()
    assert set(notes) == {"jingles", "effects"} and notes["effects"]


def test_sid_selfcheck():
    msid.selfcheck()


def test_sim_rom_is_explicit():
    prog = bytearray(ms.PROG_SIZE)
    prog[0x10:0x14] = bytes([1, 2, 3, 4])
    rom = ms.SimRom(prog, b"")
    assert rom.w(0x10) == 0x0102 and rom.l(0x10) == 0x01020304
    other = rom.with_prog(bytearray(ms.PROG_SIZE)).with_edges(lambda race, ult, c, d: d)
    assert other.l(0x10) == 0 and other.edges(0, 0, 0, 7) == 7
    assert rom.l(0x10) == 0x01020304 and rom.edges is None


def test_missing_trace_names_the_tool(tmp_path, log):
    run = marble_convert.Run(Context(marble.JOB, Paths(tmp_path), (), log), None, None)
    with pytest.raises(ConversionError, match="dev.marble.make_fixture swrite"):
        run.trace(marble_convert.SPRITE_FIXTURE, "swrite")


def test_marble_is_in_a_partial_run():
    assert "marble" in {j.name for j in resolve(["marble"])}


def _repo(base, real):
    """A repo root with the real ROMs and the tracked MAME traces, no outputs."""
    base.mkdir()
    (base / "romszip").symlink_to(real.romszip)
    for directory, name in marble_convert.TRACE_INPUTS:
        source = real.test / directory / name
        if not source.is_file():
            pytest.skip(f"{source} missing")
        target = base / "source" / "test" / directory / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.symlink_to(source)
    return Paths(base)


def _outputs(paths):
    files = [paths.machines / "marble" / "marble_rom.h"]
    files += [paths.test / d / n for d, n in marble_convert.OUTPUT_FIXTURES]
    return [f.read_bytes() for f in files]


@pytest.mark.slow
def test_repeatable_in_process(tmp_path, log):
    """The conversion keeps no state between runs: two runs in one interpreter write the same files."""
    real = default()
    for name in ("marble.zip", "atarisy1.zip", "Marble_Madness.sid"):
        if not (real.romszip / name).is_file():
            pytest.skip(f"romszip/{name} missing")

    runs = []
    for name in ("first", "second"):
        paths = _repo(tmp_path / name, real)
        marble.JOB.convert(Context(marble.JOB, paths, ("marble",), log))
        runs.append(_outputs(paths))
        assert (paths.tmp / "marble" / "marble.sid").is_file()

    assert runs[0] == runs[1]
    assert len(runs[0][0]) > 600_000 and all(runs[0])
