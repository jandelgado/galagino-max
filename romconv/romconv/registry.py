from collections.abc import Sequence

from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.remote import bundles, catalog

ALL: tuple[Job, ...] = (catalog.JOB, bundles.JOB)


def by_name(name: str, jobs: Sequence[Job] | None = None) -> Job:
    jobs = ALL if jobs is None else jobs
    for job in jobs:
        if job.name == name:
            return job
    raise ConversionError(f"unknown job {name!r}; valid: {', '.join(j.name for j in jobs)}")


def resolve(names: Sequence[str], jobs: Sequence[Job] | None = None) -> list[Job]:
    """Jobs to run, in registry order: all if names is empty, else the named
    ones plus their deps (transitively) plus every phase 2 job."""
    jobs = ALL if jobs is None else jobs
    if not names:
        return list(jobs)
    wanted = {j.name for j in jobs if j.phase == 2}
    todo = list(names)
    while todo:
        job = by_name(todo.pop(), jobs)
        if job.name not in wanted:
            wanted.add(job.name)
            todo.extend(job.deps)
    return [j for j in jobs if j.name in wanted]
