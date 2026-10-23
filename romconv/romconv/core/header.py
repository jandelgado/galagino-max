import io
from pathlib import Path
from types import TracebackType

from romconv.core.manifest import Manifest


class HeaderWriter:
    """Text sink for one generated header; the file appears on clean close only."""

    def __init__(self, path: Path, machine: str, plan_game: str | None, remote_dir: str | None,
                 manifest: Manifest) -> None:
        self.path = path
        self.machine = machine
        self.plan_game = plan_game
        self.remote_dir = remote_dir
        self.manifest = manifest
        self._buffer = io.StringIO()

    def write(self, s: str) -> int:
        return self._buffer.write(s)

    def __enter__(self) -> "HeaderWriter":
        return self

    def __exit__(self, exc_type: type[BaseException] | None, exc: BaseException | None,
                 tb: TracebackType | None) -> None:
        if exc_type is not None:
            return
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.path, "w", encoding="utf-8", newline="\n") as f:
            f.write(self._buffer.getvalue())
