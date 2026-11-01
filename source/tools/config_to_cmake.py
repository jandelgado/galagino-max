#!/usr/bin/env python3
"""Writes the firmware.toml build config of one env as a CMake include.

  [GAMES=a,b] [DEFINES="A=1 B"] python3 tools/config_to_cmake.py pico2w_dvi-remote > build/<x>/galagino.cmake

Sets GALAGINO_BOARD (the env's board), GALAGINO_DEFINES (firmware_config.env_defines:
platform, board, options, mode, layout, DEFINES) and GALAGINO_SOURCES (paths relative
to src/: the platform's HAL backend; src/net/ only for ASSETS_REMOTE builds).
GAMES replaces the env's games, as in the PlatformIO build.
"""
import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import firmware_config  # noqa: E402

_SRC_SUFFIXES = {".c", ".cpp"}
_NET_DIR = "net"


def _sources(src_root, hal, defines):
    """Relative paths of the .c/.cpp files of one HAL backend; net/ for ASSETS_REMOTE builds."""
    root = Path(src_root)
    net = "ASSETS_REMOTE" in defines
    found = []
    for path in sorted(root.rglob("*")):
        if path.suffix not in _SRC_SUFFIXES:
            continue
        parts = path.relative_to(root).parts
        if parts[0] == "hal" and len(parts) > 2 and parts[1] != hal:
            continue
        if parts[0] == _NET_DIR and not net:
            continue
        found.append(path.relative_to(root).as_posix())
    return found


def _emit(board, flags, sources):
    def block(name, items):
        return "set(%s\n%s\n)\n" % (name, "\n".join("    " + i for i in items))
    return "set(GALAGINO_BOARD %s)\n" % board + block("GALAGINO_DEFINES", flags) + block("GALAGINO_SOURCES", sources)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("env", help="[envs] entry of firmware.toml")
    args = ap.parse_args()

    games, extra = firmware_config.dev_overrides(os.environ)
    try:
        cfg = firmware_config.load()
        row = firmware_config.env(cfg, args.env)
        defines = firmware_config.env_defines(cfg, args.env, games, extra)
    except firmware_config.ConfigError as e:
        sys.exit("config_to_cmake: %s" % e)

    src = Path(__file__).resolve().parent.parent / "src"
    sys.stdout.write(_emit(row.get("board", ""), defines, _sources(src, row["platform"], defines)))


if __name__ == "__main__":
    main()
