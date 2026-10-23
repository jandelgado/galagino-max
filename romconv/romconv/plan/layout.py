# Arena block sizes per layout, read from source/src/emulation/arena_layout.h
# (the C++ side uses the same file). An allocation cannot span blocks.
import re
from pathlib import Path

from romconv.core.paths import Paths


def header_path(paths: Paths) -> Path:
    return paths.src / "emulation" / "arena_layout.h"


def read_layouts(path: Path) -> dict[str, tuple[int, int]]:
    """{"local": (a, b), "remote": (a, b)}; ValueError if a define is missing."""
    defs = dict(re.findall(r"^#define (ARENA_\w+) (\d+)$", Path(path).read_text(encoding="utf-8"), re.M))
    try:
        b = int(defs["ARENA_B_SIZE"])
        return {"local": (int(defs["ARENA_A_LOCAL"]), b),
                "remote": (int(defs["ARENA_A_REMOTE"]), b)}
    except KeyError as e:
        raise ValueError(f"{path}: missing #define {e.args[0]}") from e
