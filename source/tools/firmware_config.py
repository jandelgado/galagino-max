"""Reads firmware.toml (envs, options, Arena layouts, game sets, modes) and
turns an env into its build defines. Used by extraScript.py (PlatformIO) and
config_to_cmake.py (RP2350). romconv keeps its own [arena] reader."""
import re
import tomllib
from pathlib import Path

CONFIG_PATH = Path(__file__).resolve().parent.parent / "firmware.toml"

_LAYOUT_NAME = re.compile(r"^[a-z][a-z0-9_]*$")
_REMOTE_DEFINE = "ASSETS_REMOTE"
_REMOTE_SUFFIX = "_remote"
_GAME_PREFIX = "ENABLE_"
# [envs] platform -> HAL define, also the src/hal/<platform>/ backend
PLATFORMS = {"esp": "HAL_ESP", "rp2350": "HAL_RP2350", "host": "HAL_HOST"}
_ENV_KEYS = {"platform", "board", "mode", "layout"}


class ConfigError(ValueError):
    pass


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

    return list(modes[mode].get("defines", [])) + [_GAME_PREFIX + g.upper() for g in enabled]


# ENABLE_<GAME> of a dev build's game list (GAMES=a,b), each a game of [games]
def _game_defines(cfg, games):
    known = {g for names in cfg.get("games", {}).values() for g in names}
    unknown = [g for g in games if g not in known]
    if unknown:
        raise ConfigError("firmware.toml: games %s not in [games]" % ", ".join(unknown))
    return [_GAME_PREFIX + g.upper() for g in games]


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


# one [envs] row: platform, layout; a device env also board and mode
def env(cfg, name):
    envs = cfg.get("envs", {})
    if name not in envs:
        raise ConfigError("firmware.toml: env '%s' not in [envs]: %s" % (name, ", ".join(envs)))

    row = envs[name]
    extra = set(row) - _ENV_KEYS
    if extra:
        raise ConfigError("firmware.toml: [envs] %s: unknown keys %s" % (name, ", ".join(sorted(extra))))
    if row.get("platform") not in PLATFORMS:
        raise ConfigError("firmware.toml: [envs] %s: platform must be one of %s"
                          % (name, ", ".join(PLATFORMS)))
    if "layout" not in row:
        raise ConfigError("firmware.toml: [envs] %s: layout missing" % name)
    device = row["platform"] != "host"
    if device and not ("board" in row and "mode" in row):
        raise ConfigError("firmware.toml: [envs] %s: a device env needs board and mode" % name)
    return row


# Defines of one env: HAL_<PLATFORM>, BOARD_<NAME>, the options, the mode's
# defines and games, the layout, then extra. games (a dev build, GAMES=a,b)
# replaces the mode's games.
def env_defines(cfg, name, games=None, extra=()):
    row = env(cfg, name)
    defines = [PLATFORMS[row["platform"]]]
    if "board" in row:
        defines.append("BOARD_" + row["board"].upper())
    if "mode" in row:
        mode = mode_defines(cfg, row["mode"])
        if games is not None:
            mode = [d for d in mode if not d.startswith(_GAME_PREFIX)] + _game_defines(cfg, games)
        defines += option_defines(cfg) + mode
    elif games is not None:
        raise ConfigError("firmware.toml: env '%s' has no mode, GAMES does not apply" % name)

    check_layout_mode(row["layout"], defines + list(extra))
    return defines + layout_defines(cfg, row["layout"]) + list(extra)


# A dev build's overrides from the environment: GAMES=a,b replaces the env's
# games, DEFINES="A=1 B" adds defines. Both builds read them the same way.
def dev_overrides(environ):
    games = environ.get("GAMES", "").strip()
    return ([g for g in games.split(",") if g] if games else None), environ.get("DEFINES", "").split()
