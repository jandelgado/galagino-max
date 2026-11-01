#!/usr/bin/env python3
"""Writes the resolved platformio.ini build config of one env as a CMake include.

  python3 tools/pio_to_cmake.py <env> [--only pacman,galaga] [--hal rp2350] [--no-net]
      [--define NAME[=V]] [--config file.json] > build/<env>.cmake

Sets GALAGINO_DEFINES (the -D flags) and GALAGINO_SOURCES (paths relative to src/).
Source of truth is `pio project config --json-output`.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

# Arduino / toolchain specific, no meaning outside the Arduino build
_DROP = {"ARDUINO_RUNNING_CORE", "CORE_DEBUG_LEVEL"}
_HAL_DEFINES = {"esp": "HAL_ESP", "rp2350": "HAL_RP2350", "host": "HAL_HOST"}
_SRC_SUFFIXES = {".c", ".cpp"}


def _parse_flags(config_json, env):
    """Defines of [env:<env>] as NAME or NAME=VALUE; non -D flags are dropped."""
    text = config_json[config_json.index("["):]
    sections = {name: dict(items) for name, items in json.loads(text)}
    section = sections.get("env:" + env)
    if section is None:
        sys.exit("pio_to_cmake: no env '%s'" % env)
    flags = []
    for raw in section.get("build_flags", []):
        raw = raw.strip()
        if not raw.startswith("-D"):
            continue
        define = raw[2:].strip().replace("$PIOENV", env)
        if define.split("=")[0] in _DROP:
            continue
        flags.append(define)
    return flags


def _only(flags, games):
    """Replaces the ENABLE_* game flags with the named games."""
    kept = [f for f in flags if not f.startswith("ENABLE_")]
    return kept + ["ENABLE_" + g.upper() for g in games]


def _hal(flags, hal):
    return [f for f in flags if f not in _HAL_DEFINES.values()] + [_HAL_DEFINES[hal]]


def _sources(src_root, hal, net):
    """Relative paths of the .c/.cpp files of one HAL backend."""
    root = Path(src_root)
    found = []
    for path in sorted(root.rglob("*")):
        if path.suffix not in _SRC_SUFFIXES:
            continue
        rel = path.relative_to(root)
        parts = rel.parts
        if parts[0] == "hal" and len(parts) > 2 and parts[1] != hal:
            continue
        if parts[0] == "net" and not net:
            continue
        found.append(rel.as_posix())
    return found


def _emit(flags, sources):
    def block(name, items):
        return "set(%s\n%s\n)\n" % (name, "\n".join("    " + i for i in items))
    return block("GALAGINO_DEFINES", flags) + block("GALAGINO_SOURCES", sources)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("env")
    ap.add_argument("--only", help="comma separated games replacing the ENABLE_* flags")
    ap.add_argument("--hal", choices=sorted(_HAL_DEFINES))
    ap.add_argument("--no-net", action="store_true")
    ap.add_argument("--define", action="append", default=[])
    ap.add_argument("--config", help="resolved config json instead of running pio")
    args = ap.parse_args()

    tools = Path(__file__).resolve().parent
    if args.config:
        config = Path(args.config).read_text()
    else:
        config = subprocess.run(["pio", "project", "config", "--json-output"], cwd=tools.parent,
                                check=True, capture_output=True, text=True).stdout
    flags = _parse_flags(config, args.env)
    if args.only:
        flags = _only(flags, args.only.split(","))
    hal = args.hal or ("rp2350" if "HAL_RP2350" in flags else "host" if "HAL_HOST" in flags else "esp")
    flags = _hal(flags, hal) + args.define
    sys.stdout.write(_emit(flags, _sources(tools.parent / "src", hal, not args.no_net)))


if __name__ == "__main__":
    main()
