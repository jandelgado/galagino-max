from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Paths:
    """Repo-derived locations; nothing depends on the working directory."""

    repo: Path

    @property
    def romszip(self) -> Path:
        return self.repo / "romszip"

    @property
    def src(self) -> Path:
        return self.repo / "source" / "src"

    @property
    def machines(self) -> Path:
        return self.src / "machines"

    @property
    def server(self) -> Path:
        return self.repo / "galagino-server" / "data"

    @property
    def build(self) -> Path:
        return self.repo / "romconv" / "build"

    @property
    def assets(self) -> Path:
        return self.build / "assets"

    @property
    def manifests(self) -> Path:
        return self.build / "manifest"

    @property
    def tmp(self) -> Path:
        return self.build / "tmp"


def default() -> Paths:
    return Paths(Path(__file__).resolve().parents[3])
