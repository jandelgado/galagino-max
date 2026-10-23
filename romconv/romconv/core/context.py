import hashlib
import logging
import zipfile
from collections.abc import Sequence
from pathlib import Path

from romconv.core.errors import ConversionError
from romconv.core.header import HeaderWriter
from romconv.core.job import Job
from romconv.core.manifest import Manifest
from romconv.core.paths import Paths

# Machine dirs whose assets several games share: always built in
SHARED_DIRS = frozenset({"namco54", "williams"})


class Context:
    """Everything a converter needs: ROM input, output files, log."""

    def __init__(self, job: Job, paths: Paths, requested: tuple[str, ...], log: logging.Logger) -> None:
        self.job = job
        self.paths = paths
        self.requested = requested
        self.log = log
        self.manifest = Manifest()

    @property
    def tmp(self) -> Path:
        path = self.paths.tmp / self.job.name
        path.mkdir(parents=True, exist_ok=True)
        return path

    def zip_path(self, zip: str) -> Path:
        return self.paths.romszip / zip

    def rom(self, names: str | Sequence[str], sha1: str | None = None, zip: str | None = None) -> bytearray:
        data = self.rom_optional(names, sha1, zip)
        if data is None:
            zips = [zip] if zip is not None else list(self.job.zips)
            absent = [z for z in zips if not self.zip_path(z).is_file()]
            note = f" (zip not found: {', '.join(absent)})" if absent else ""
            raise ConversionError(f"{', '.join(zips) or '(no zips declared)'}: "
                                  f"no file {self._listing(names)}{note}")
        return data

    def rom_optional(self, names: str | Sequence[str], sha1: str | None = None,
                     zip: str | None = None) -> bytearray | None:
        names = [names] if isinstance(names, str) else list(names)
        zips = [zip] if zip is not None else list(self.job.zips)
        mismatches = []
        for name in names:
            for z in zips:
                data = self._read_member(z, name)
                if data is None:
                    continue
                if sha1 is not None and hashlib.sha1(data).hexdigest() != sha1.lower():
                    mismatches.append(f"{z}: {name}: SHA-1 mismatch (expected {sha1})")
                    continue
                return bytearray(data)
        if mismatches:
            raise ConversionError("; ".join(mismatches))
        return None

    def _read_member(self, zip: str, name: str) -> bytes | None:
        path = self.zip_path(zip)
        if not path.is_file():
            return None
        with zipfile.ZipFile(path) as z:
            try:
                return z.read(name)
            except KeyError:
                return None

    @staticmethod
    def _listing(names: str | Sequence[str]) -> str:
        return names if isinstance(names, str) else " or ".join(names)

    def header(self, filename: str, machine: str | None = None, plan: bool = True,
               remote: bool = True) -> HeaderWriter:
        dir = self._machine(machine)
        return HeaderWriter(
            path=self.paths.machines / dir / filename,
            machine=dir,
            plan_game=self.job.name if self.job.plan and plan else None,
            remote_dir=dir if remote and dir not in SHARED_DIRS else None,
            manifest=self.manifest,
        )

    def path(self, filename: str, machine: str | None = None) -> Path:
        return self.paths.machines / self._machine(machine) / filename

    def _machine(self, machine: str | None) -> str:
        dir = machine or self.job.machine
        if dir is None:
            raise ConversionError(f"job {self.job.name}: no machine dir (set Job.machine or pass machine=)")
        return dir
