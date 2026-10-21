from collections.abc import Sequence

from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.cpu import m6502, z80
from romconv.games import (anteater, bagman, crush, digdug, dkong, eyes, frogger, galaga, galaxian, game_1942,
                           lizwiz, mrtnt, pacman, theglob, vanvan)
from romconv.games.namco import digdug2, gaplus, mappy, phozon, todruaga, xevious
from romconv.gfx import logo
from romconv.hw import namco54
from romconv.misc import pokey_poly, unpack_fixture
from romconv.remote import bundles, catalog

ALL: tuple[Job, ...] = (
    pokey_poly.JOB,
    unpack_fixture.JOB,
    z80.JOB,
    m6502.JOB,
    logo.LOGOS_JOB,
    namco54.JOB,
    game_1942.JOB,
    anteater.JOB,
    bagman.JOB,
    crush.JOB,
    digdug.JOB,
    dkong.JOB,
    eyes.JOB,
    frogger.JOB,
    galaga.JOB,
    lizwiz.JOB,
    mrtnt.JOB,
    pacman.JOB,
    theglob.JOB,
    vanvan.JOB,
    *galaxian.JOBS,
    mappy.JOB,
    gaplus.JOB,
    todruaga.JOB,
    phozon.JOB,
    *digdug2.JOBS,
    *xevious.JOBS,
    catalog.JOB,
    bundles.JOB,
)


def by_name(name: str, jobs: Sequence[Job] | None = None) -> Job:
    jobs = ALL if jobs is None else jobs
    for job in jobs:
        if job.name == name:
            return job
    raise ConversionError(f"unknown job {name!r}; valid: {', '.join(j.name for j in jobs)}")


def resolve(names: Sequence[str], jobs: Sequence[Job] | None = None) -> list[Job]:
    """Jobs to run, in registry order: all if names is empty, else the named
    ones plus their deps (transitively) plus every phase 2 and `always` job."""
    jobs = ALL if jobs is None else jobs
    if not names:
        return list(jobs)
    todo = [j.name for j in jobs if j.phase == 2 or j.always] + list(names)
    wanted: set[str] = set()
    while todo:
        job = by_name(todo.pop(), jobs)
        if job.name not in wanted:
            wanted.add(job.name)
            todo.extend(job.deps)
    return [j for j in jobs if j.name in wanted]
