#!/usr/bin/env python3
"""Writes the firmware.toml build config of one mode + Arena layout as a CMake include.

  python3 tools/config_to_cmake.py --mode local --layout rp2350 --hal rp2350
      [--only pacman,galaga] [--define NAME[=V]] > build/<x>/galagino.cmake

Sets GALAGINO_DEFINES (HAL_<X>, the options, the mode's defines and games, the
layout's ARENA_* defines, --define) and GALAGINO_SOURCES (paths relative to
src/; src/net/ only for ASSETS_REMOTE builds).
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import firmware_config  # noqa: E402

_HAL_DEFINES = {"esp": "HAL_ESP", "rp2350": "HAL_RP2350", "host": "HAL_HOST"}
_SRC_SUFFIXES = {".c", ".cpp"}
_GAME_PREFIX = "ENABLE_"
_NET_DIR = "net"


def _defines(cfg, mode, layout, hal, only, extra):
    """only: games replacing the mode's ENABLE_* defines, None keeps them."""
    try:
        defines = firmware_config.build_defines(cfg, mode, layout)
    except firmware_config.ConfigError as e:
        sys.exit("config_to_cmake: %s" % e)

    if only is not None:
        layout_at = next(i for i, d in enumerate(defines) if d.startswith("ARENA_LAYOUT_"))
        kept = [d for d in defines[:layout_at] if not d.startswith(_GAME_PREFIX)]
        defines = kept + [_GAME_PREFIX + g.upper() for g in only] + defines[layout_at:]

    return [_HAL_DEFINES[hal]] + defines + list(extra)


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


def _emit(flags, sources):
    def block(name, items):
        return "set(%s\n%s\n)\n" % (name, "\n".join("    " + i for i in items))
    return block("GALAGINO_DEFINES", flags) + block("GALAGINO_SOURCES", sources)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--mode", required=True, help="[modes] entry of firmware.toml")
    ap.add_argument("--layout", required=True, help="[arena] row of firmware.toml")
    ap.add_argument("--hal", required=True, choices=sorted(_HAL_DEFINES))
    ap.add_argument("--only", help="comma separated games replacing the mode's games")
    ap.add_argument("--define", action="append", default=[])
    args = ap.parse_args()

    try:
        cfg = firmware_config.load()
    except firmware_config.ConfigError as e:
        sys.exit("config_to_cmake: %s" % e)

    only = args.only.split(",") if args.only else None
    defines = _defines(cfg, args.mode, args.layout, args.hal, only, args.define)
    src = Path(__file__).resolve().parent.parent / "src"
    sys.stdout.write(_emit(defines, _sources(src, args.hal, defines)))


if __name__ == "__main__":
    main()
