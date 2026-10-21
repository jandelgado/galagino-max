import json
import shutil
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Asset:
    """One emitted FlashAsset.

    mode: PLAIN, COMPRESSED or PLANNED. packed is the full-dictionary LZMA
    stream in every mode; lz64 the 64 KB-dictionary stream (None for
    COMPRESSED). hint: the converter's own mode for PLANNED, else None."""

    dir: str
    name: str
    ctype: str
    inner: str
    count: int
    mode: str
    raw: bytes
    packed: bytes
    lz64: bytes | None
    hint: str | None


class Manifest:
    def __init__(self) -> None:
        self._assets: list[Asset] = []

    def add(self, asset: Asset) -> None:
        self._assets.append(asset)

    def assets(self, dir: str | None = None) -> list[Asset]:
        return [a for a in self._assets if dir is None or a.dir == dir]

    def dirs(self) -> set[str]:
        return {a.dir for a in self._assets}

    def persist(self, root: Path) -> None:
        """Write root/<dir>/<name>.{json,raw,lz[,lz64]}, replacing each dir."""
        for dir in sorted(self.dirs()):
            out = root / dir
            shutil.rmtree(out, ignore_errors=True)
            out.mkdir(parents=True)
            for a in self.assets(dir):
                meta = {"ctype": a.ctype, "inner": a.inner, "count": a.count, "mode": a.mode}
                (out / f"{a.name}.json").write_bytes(json.dumps(meta).encode())
                (out / f"{a.name}.raw").write_bytes(a.raw)
                (out / f"{a.name}.lz").write_bytes(a.packed)
                if a.lz64 is not None:
                    (out / f"{a.name}.lz64").write_bytes(a.lz64)
