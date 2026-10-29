"""Reads firmware.toml (options, Arena layouts, game sets, modes) and turns a
mode + layout into the build defines. Used by extraScript.py (PlatformIO) and
config_to_cmake.py (RP2350). romconv keeps its own [arena] reader."""
import enum
import re
import tomllib
from pathlib import Path

CONFIG_PATH = Path(__file__).resolve().parent.parent / "firmware.toml"

_LAYOUT_NAME = re.compile(r"^[a-z][a-z0-9_]*$")
_REMOTE_DEFINE = "ASSETS_REMOTE"
_REMOTE_SUFFIX = "_remote"


class ConfigError(ValueError):
    pass


class Target(enum.Enum):
    DEVICE = "device"
    NATIVE = "native"


def load(path=CONFIG_PATH):
    try:
        with open(path, "rb") as f:
            return tomllib.load(f)
    except (OSError, tomllib.TOMLDecodeError) as e:
        raise ConfigError("%s: %s" % (path, e)) from None


def option_defines(cfg):
    return list(cfg.get("options", {}).get("defines", []))


# the mode's own defines, then ENABLE_<GAME> of its game sets in order, each
# game once
def mode_defines(cfg, mode):
    modes = cfg.get("modes", {})
    if mode not in modes:
        raise ConfigError("firmware.toml: mode '%s' not in [modes]: %s" % (mode, ", ".join(modes)))

    games = cfg.get("games", {})
    enabled = []
    for name in modes[mode].get("games", []):
        if name not in games:
            raise ConfigError("firmware.toml: [modes] %s: game set '%s' not in [games]: %s"
                              % (mode, name, ", ".join(games)))
        enabled += [g for g in games[name] if g not in enabled]

    return list(modes[mode].get("defines", [])) + ["ENABLE_" + g.upper() for g in enabled]


def _is_size(v):
    return isinstance(v, int) and not isinstance(v, bool)


# ARENA_LAYOUT_<NAME>, ARENA_A_SIZE, ARENA_B_SIZE of one [arena] row
def layout_defines(cfg, name):
    arena = cfg.get("arena", {})
    if name not in arena:
        raise ConfigError("firmware.toml: layout '%s' not in [arena]: %s" % (name, ", ".join(arena)))

    row = arena[name]
    valid = (_LAYOUT_NAME.match(name) and isinstance(row, list) and len(row) == 2
             and all(_is_size(v) for v in row) and row[0] > 0 and row[1] >= 0)
    if not valid:
        raise ConfigError("firmware.toml: [arena] %s = %r: expected [A, B] bytes, A > 0, B >= 0, "
                          "name a lower-case C identifier" % (name, row))

    return ["ARENA_LAYOUT_" + name.upper(), "ARENA_A_SIZE=%d" % row[0], "ARENA_B_SIZE=%d" % row[1]]


# a _remote layout plans for the smaller boot heap of an ASSETS_REMOTE build
# and gets bundles; any other pairing builds something that cannot run or
# cannot download
def check_layout_mode(layout, defines):
    remote = _REMOTE_DEFINE in defines
    for_remote = layout.endswith(_REMOTE_SUFFIX)
    if for_remote == remote:
        return
    raise ConfigError("firmware.toml: layout '%s' is for %s builds but %s is %s"
                      % (layout, "remote" if for_remote else "local", _REMOTE_DEFINE,
                         "set" if remote else "not set"))


def build_defines(cfg, mode, layout):
    defines = option_defines(cfg) + mode_defines(cfg, mode)
    check_layout_mode(layout, defines)
    return defines + layout_defines(cfg, layout)


# Defines the PlatformIO hook appends: an env with custom_mode gets
# build_defines(); one without (perf envs, own ENABLE_ flags) the options and
# the layout; env:native only the layout. own: the env's own -D names, part
# of the layout check.
def hook_defines(cfg, mode, layout, target, own=()):
    defines = [] if target == Target.NATIVE else option_defines(cfg)
    if mode is not None:
        defines += mode_defines(cfg, mode)
    check_layout_mode(layout, defines + list(own))
    return defines + layout_defines(cfg, layout)
