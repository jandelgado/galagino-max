# Arena block sizes per layout: [arena] in source/platformio.ini, the same
# table the firmware builds with (extraScript.py turns custom_arena_layout
# into ARENA_LAYOUT_<NAME>, ARENA_A_SIZE, ARENA_B_SIZE). An allocation
# cannot span blocks.
import configparser
import re
from pathlib import Path

from romconv.core.paths import Paths

_SECTION = "arena"
_NAME = re.compile(r"^[a-z][a-z0-9_]*$")
_REMOTE_SUFFIX = "_remote"


def ini_path(paths: Paths) -> Path:
    return paths.repo / "source" / "platformio.ini"


def read_layouts(path: Path) -> dict[str, tuple[int, int]]:
    """{name: (a, b)} in ini order; ValueError for a missing section or a bad row."""
    config = configparser.ConfigParser(interpolation=None)
    config.read(path, encoding="utf-8")
    if not config.has_section(_SECTION) or not config.items(_SECTION):
        raise ValueError(f"{path}: no [{_SECTION}] section")

    layouts = {}
    for name, value in config.items(_SECTION):
        tokens = value.split()
        ok = (_NAME.match(name) and len(tokens) == 2 and all(t.isdigit() for t in tokens)
              and int(tokens[0]) > 0)
        if not ok:
            raise ValueError(f"{path}: [{_SECTION}] {name} = {value}: expected a C identifier"
                             " = 'A B' bytes, A > 0, B >= 0")
        layouts[name] = (int(tokens[0]), int(tokens[1]))
    return layouts


def remote_layouts(layouts: dict[str, tuple[int, int]]) -> list[str]:
    """Names ending in _remote: ASSETS_REMOTE builds, which get bundles."""
    return [name for name in layouts if name.endswith(_REMOTE_SUFFIX)]


def define_name(layout: str) -> str:
    """The build define that selects this layout's block in generated headers."""
    return "ARENA_LAYOUT_" + layout.upper()
