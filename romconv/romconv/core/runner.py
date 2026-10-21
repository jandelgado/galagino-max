import importlib
import io
import logging
import sys
import time
import traceback
from collections.abc import Sequence
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import dataclass
from types import ModuleType

from romconv import registry as _default_registry
from romconv.core import paths as _paths
from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.paths import Paths

_DEFAULT_REGISTRY = "romconv.registry"


@dataclass
class JobResult:
    name: str
    ok: bool
    log: str
    seconds: float
    error: str | None
    dirs: tuple[str, ...] = ()


def _job_logger(name: str) -> tuple[logging.Logger, io.StringIO]:
    buffer = io.StringIO()
    handler = logging.StreamHandler(buffer)
    handler.setFormatter(logging.Formatter("%(message)s"))
    log = logging.Logger(name, logging.INFO)  # not registered globally
    log.addHandler(handler)
    return log, buffer


def _run_job(name: str, requested: tuple[str, ...], paths: Paths,
             registry_name: str = _DEFAULT_REGISTRY) -> JobResult:
    """Worker entry: run one job by name, never raises."""
    start = time.perf_counter()
    log, buffer = _job_logger(name)
    dirs: tuple[str, ...] = ()
    error = None
    try:
        jobs = importlib.import_module(registry_name).ALL
        job = _default_registry.by_name(name, jobs)
        ctx = Context(job, paths, requested, log)
        job.convert(ctx)
        if job.plan:
            from romconv.plan.arena import write_arena_plan

            write_arena_plan(ctx)
        ctx.manifest.persist(paths.assets)
        dirs = tuple(sorted(ctx.manifest.dirs()))
    except ConversionError as e:
        error = str(e)
    except Exception as e:
        log.error(traceback.format_exc().rstrip())
        error = f"{type(e).__name__}: {e}"
    return JobResult(name, error is None, buffer.getvalue(), time.perf_counter() - start, error, dirs)


def _report(result: JobResult) -> None:
    if result.log:
        print(result.log.rstrip("\n"), file=sys.stdout)
    status = "ok" if result.ok else f"FAILED: {result.error}"
    print(f"-- {result.name}: {status} ({result.seconds:.1f}s)", file=sys.stdout, flush=True)


def _collect(results: list[JobResult], owners: dict[str, str]) -> None:
    for r in results:
        for d in r.dirs:
            if d in owners and owners[d] != r.name:
                raise ConversionError(f"asset dir {d!r} registered by both {owners[d]} and {r.name}")
            owners[d] = r.name


def _run_phase1(names: list[str], requested: tuple[str, ...], jobs: int, paths: Paths,
                registry_name: str, mp_context) -> list[JobResult]:
    results = []
    if jobs <= 1:
        for name in names:
            result = _run_job(name, requested, paths, registry_name)
            _report(result)
            results.append(result)
        return results
    with ProcessPoolExecutor(max_workers=jobs, mp_context=mp_context) as pool:
        futures = {pool.submit(_run_job, n, requested, paths, registry_name): n for n in names}
        for future in as_completed(futures):
            try:
                result = future.result()
            except Exception as e:  # worker died or result not picklable
                result = JobResult(futures[future], False, "", 0.0, f"{type(e).__name__}: {e}")
            _report(result)
            results.append(result)
    return results


def run(names: Sequence[str], jobs: int, paths: Paths | None = None, registry: ModuleType | None = None,
        mp_context=None, requested: Sequence[str] | None = None) -> int:
    """Run the named jobs (all if empty); 0 if every job succeeded, else 1.

    registry: module with ALL (default romconv.registry). Phase 2 jobs run
    in-process after all phase 1 jobs succeeded. requested: what the user
    asked for, as seen by Context.requested; default the names, or every job
    of the registry for a full run."""
    paths = paths or _paths.default()
    registry_name = registry.__name__ if registry is not None else _DEFAULT_REGISTRY
    all_jobs = importlib.import_module(registry_name).ALL
    selected = _default_registry.resolve(names, all_jobs)
    if requested is None:
        requested = names or [j.name for j in all_jobs]
    requested = tuple(requested)
    owners: dict[str, str] = {}
    failed: list[JobResult] = []

    phase1 = [j.name for j in selected if j.phase == 1]
    results = _run_phase1(phase1, requested, jobs, paths, registry_name, mp_context)
    _collect(results, owners)
    failed += [r for r in results if not r.ok]

    if not failed:
        for job in sorted((j for j in selected if j.phase > 1), key=lambda j: j.phase):
            result = _run_job(job.name, requested, paths, registry_name)
            _report(result)
            _collect([result], owners)
            if not result.ok:
                failed.append(result)
                break

    for r in failed:
        print(f"FAILED {r.name}: {r.error}", file=sys.stdout)
    return 1 if failed else 0
