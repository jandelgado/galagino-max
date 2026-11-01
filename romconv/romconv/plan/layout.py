# Arena block sizes per layout: [arena] in source/firmware.toml, the same
# table the firmware builds with (the PlatformIO hook and the RP2350 build turn
# the selected layout into ARENA_LAYOUT_<NAME>, ARENA_A_SIZE, ARENA_B_SIZE).
# An allocation cannot span blocks. Own reader: romconv never imports source/.
import re
import tomllib
from pathlib import Path

from romconv.core.paths import Paths

_SECTION = "arena"
_NAME = re.compile(r"^[a-z][a-z0-9_]*$")
_REMOTE_SUFFIX = "_remote"
_MAX_START = "max = [\n"
_MAX_END = "]\n"


def config_path(paths: Paths) -> Path:
    return paths.repo / "source" / "firmware.toml"


def _load(path: Path) -> dict:
    try:
        with open(path, "rb") as f:
            return tomllib.load(f)
    except (OSError, tomllib.TOMLDecodeError) as e:
        raise ValueError(f"{path}: {e}") from None


def _is_size(v) -> bool:
    return isinstance(v, int) and not isinstance(v, bool)


def read_layouts(path: Path) -> dict[str, tuple[int, int]]:
    """{name: (a, b)} in file order; ValueError for a missing section or a bad row."""
    rows = _load(path).get(_SECTION)
    if not rows:
        raise ValueError(f"{path}: no [{_SECTION}] section")

    layouts = {}
    for name, row in rows.items():
        ok = (_NAME.match(name) and isinstance(row, list) and len(row) == 2 and all(_is_size(v) for v in row)
              and row[0] > 0 and row[1] >= 0)
        if not ok:
            raise ValueError(f"{path}: [{_SECTION}] {name} = {row!r}: expected a C identifier"
                             " = [A, B] bytes, A > 0, B >= 0")
        layouts[name] = (row[0], row[1])
    return layouts


def read_max_list(path: Path) -> list[str]:
    """[games] max: the games of the 4 MB max build, file order."""
    return list(_load(path)["games"]["max"])


def write_max_list(path: Path, games: list[str]) -> None:
    """Replaces only the max array's lines, the rest of the file stays as it is."""
    text = path.read_text()
    a = text.index("\n" + _MAX_START) + 1 + len(_MAX_START)
    b = text.index(_MAX_END, a)
    path.write_text(text[:a] + "".join(f'  "{g}",\n' for g in games) + text[b:])


def remote_layouts(layouts: dict[str, tuple[int, int]]) -> list[str]:
    """Names ending in _remote: ASSETS_REMOTE builds, which get bundles."""
    return [name for name in layouts if name.endswith(_REMOTE_SUFFIX)]


def min_blocks(layouts: dict[str, tuple[int, int]]) -> tuple[int, int]:
    """Smallest A and smallest B over every row: an unplanned game's hand split must fit each build."""
    return min(a for a, _ in layouts.values()), min(b for _, b in layouts.values())


def min_capacity(layouts: dict[str, tuple[int, int]], remote: bool) -> int:
    """Smallest A + B over the rows a game builds for: every row for a catalog
    (remote) game, the non-_remote rows otherwise."""
    return min(a + b for name, (a, b) in layouts.items() if remote or not name.endswith(_REMOTE_SUFFIX))


def define_name(layout: str) -> str:
    """The build define that selects this layout's block in generated headers."""
    return "ARENA_LAYOUT_" + layout.upper()
