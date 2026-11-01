"""Pick [games_max] of source/platformio.ini: the most games in 4 MB.

    uv run python -m dev.games_max OBJDIR --built BYTES [--budget N] [--margin N]
                                   [--built-dram BYTES --dram N] [--must X,Y] [--write]

OBJDIR: objects of a firmware compiled with every game and without LTO,
one section per function (pio_perf.ini env measure-all: mode_full flags,
GAME_NO_IRAM, cmake -DGALAGINO_NO_LTO=1; its link fails on purpose, the
ESP32 maps 4 MB of flash, the objects under .pio/build/measure-all/src
remain). --built: the image size of the azdelivery-max build of the list
that is in platformio.ini now: it calibrates the model (IDF libraries,
linker relaxation) as one offset. See romconv.plan.flashpick for the model.

--built-dram / --dram: static DRAM (dram0 data + bss, xtensa-esp32-elf-size -A)
of that build calibrates the DRAM model the same way; --dram caps it. Static
DRAM comes out of the heap Arena block A needs: a HEAP_PROBE sweep gives the
headroom, so --dram = built DRAM + (min_free - bar).
"""
import argparse
import collections
import os
import re
import subprocess
import sys
from pathlib import Path

from romconv.plan import flashpick

_REPO = Path(__file__).resolve().parents[2]
_INI = _REPO / "source" / "platformio.ini"
_MACHINES_H = _REPO / "source" / "src" / "machines.h"
_OBJDUMP = Path.home() / ".platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-objdump"
_APP0 = 0x3f0000          # partitions.csv app0
_FLASH = (".text", ".literal", ".rodata", ".iram", ".data", ".init_array")
_DRAM = (".data", ".bss", ".dram", ".noinit")
_CREATE = re.compile(r"_Z13createMachineI(\d+)([A-Za-z0-9_]+?)EP11machineBasev")
_LOGO = re.compile(r"_ZN(\d+)([A-Za-z0-9_]+?)(4logoEv|8menuLeds)")
_GUARD = re.compile(r"\s*#\s*if(?:def)?\s+(?:defined\()?GAME_([A-Z0-9_]+)")


def _objdump(args):
    return subprocess.run([str(_OBJDUMP), *args], capture_output=True, text=True, check=True).stdout


def _length_prefixed(m):
    n = int(m.group(1))
    return m.group(2)[:n] if len(m.group(2)) >= n else None


def _read_objects(objdir):
    """(sections, symbols): Section per (object, section name); symbol name
    -> key for globals, (object, name) -> key for locals."""
    sizes, refs, symbols = {}, collections.defaultdict(set), {}
    objects = sorted(Path(objdir).rglob("*.o"))
    for obj in objects:
        o = str(obj)
        for line in _objdump(["-h", o]).splitlines():
            p = line.split()
            if len(p) >= 3 and p[0].isdigit() and p[1].startswith("."):
                sizes[(o, p[1])] = int(p[2], 16)
        for line in _objdump(["-t", o]).splitlines():
            m = re.match(r"^[0-9a-f]{8} (.{7}) (\S+)\t[0-9a-f]{8} (\S+)$", line)
            if not m or m.group(2) == "*UND*":
                continue
            flags, section, name = m.groups()
            key = (o, section)
            if flags[0] == "l":
                symbols[(o, name)] = key
            else:
                symbols.setdefault(name, key)     # first definition wins, as the linker's comdat pick
        section = None
        for line in _objdump(["-r", o]).splitlines():
            m = re.match(r"RELOCATION RECORDS FOR \[(\S+)\]:", line)
            if m:
                section = (o, m.group(1))
                continue
            p = line.split()
            if section and len(p) == 3 and p[1].startswith("R_XTENSA"):
                refs[section].add(re.split(r"[+-]0x", p[2])[0])
    sections = {}
    for key, size in sizes.items():
        o, name = key
        targets = set()
        for sym in refs.get(key, ()):
            target = symbols.get((o, sym)) or symbols.get(sym)
            if target:
                targets.add(target)
        sections[key] = flashpick.Section(flash=size if name.startswith(_FLASH) else 0,
                                          dram=size if name.startswith(_DRAM) else 0,
                                          refs=frozenset(targets))
    return sections, symbols


def _classes_by_game():
    out = collections.defaultdict(set)
    game = None
    for line in _MACHINES_H.read_text().splitlines():
        m = _GUARD.match(line)
        if m:
            game = m.group(1)
        elif re.match(r"\s*#\s*endif", line):
            game = None
        for cls in re.findall(r"createMachine<([A-Za-z0-9_]+)>", line):
            if game:
                out[game].add(cls)
    return out


def _roots(sections, symbols, classes):
    """(base roots, {game: roots}): app_main and the static constructors,
    createMachine<X> / X::logo / X::menuLeds per game."""
    game_of = {cls: game for game, cl in classes.items() for cls in cl}
    base = [symbols["app_main"]] + [k for k in sections if k[1].startswith(".init_array")]
    per_game = collections.defaultdict(list)
    for sym, key in symbols.items():
        name = sym if isinstance(sym, str) else sym[1]
        m = _CREATE.match(name) or _LOGO.match(name)
        cls = _length_prefixed(m) if m else None
        if cls in game_of:
            per_game[game_of[cls]].append(key)
    return base, per_game


def _current_list():
    ini = _INI.read_text()
    block = ini[ini.index("[games_max]"):]
    block = block[:block.index("\n;", 1)] if "\n;" in block[1:] else block
    return re.findall(r"ENABLE_([A-Z0-9_]+)", block)


def _write_list(games, own):
    ini = _INI.read_text()
    a = ini.index("[games_max]")
    b = ini.index("\n;", a + 1) + 1
    body = "[games_max]\nlist =\n" + "".join(f"    -D ENABLE_{g}\n" for g in sorted(games, key=lambda g: own.get(g, 0))) + "\n"
    _INI.write_text(ini[:a] + body + ini[b:])


def _cost(groups, games):
    """(flash, dram) the model gives a game list."""
    paid = [size for m, size in groups.shared.items() if m & set(games)]
    return groups.base[0] + sum(s[0] for s in paid), groups.base[1] + sum(s[1] for s in paid)


def main(argv):
    ap = argparse.ArgumentParser(prog="dev.games_max", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("objdir")
    ap.add_argument("--built", type=int, required=True, help="image bytes of azdelivery-max with the current list")
    ap.add_argument("--budget", type=int, default=_APP0, help="app partition bytes (default app0 of partitions.csv)")
    ap.add_argument("--margin", type=int, default=16384, help="bytes kept free (default 16384)")
    ap.add_argument("--built-dram", type=int, help="static DRAM bytes of azdelivery-max with the current list")
    ap.add_argument("--dram", type=int, help="static DRAM budget (needs --built-dram)")
    ap.add_argument("--must", default="", help="comma separated games that stay in")
    ap.add_argument("--write", action="store_true", help="write the pick into platformio.ini")
    a = ap.parse_args(argv)
    if a.dram is not None and a.built_dram is None:
        ap.error("--dram needs --built-dram")

    sections, symbols = _read_objects(a.objdir)
    classes = _classes_by_game()
    base_roots, game_roots = _roots(sections, symbols, classes)
    missing = sorted(set(classes) - set(game_roots))
    if missing:
        print("not compiled into OBJDIR (not in [games]?), left out:", " ".join(missing))
    groups = flashpick.groups(sections, base_roots, game_roots)

    current = _current_list()
    model_flash, model_dram = _cost(groups, current)
    offset = a.built - model_flash
    own = {g: size[0] for m, size in groups.shared.items() for g in m if len(m) == 1}
    print(f"model: base {groups.base[0]} B + {len(groups.shared)} groups; current list {len(current)} games "
          f"-> model {model_flash} B, built {a.built} B, offset {offset:+d} B")
    dram_offset = 0
    if a.built_dram is not None:
        dram_offset = a.built_dram - model_dram
        print(f"DRAM: model {model_dram} B, built {a.built_dram} B, offset {dram_offset:+d} B")
    print("\nshared groups over 8 KB (paid once per set):")
    for m, size in sorted(groups.shared.items(), key=lambda kv: -kv[1][0]):
        if len(m) > 1 and size[0] >= 8192:
            print(f"  {size[0]:8d}  {' '.join(sorted(m)) if len(m) <= 6 else f'{len(m)} games'}")

    must = {g for g in a.must.split(",") if g}
    dram = a.dram - dram_offset if a.dram is not None else None
    chosen = flashpick.pick(groups, flash=a.budget - a.margin - offset, dram=dram, must=must)
    flash, dram_used = _cost(groups, chosen.games)
    print(f"\npick: {len(chosen.games)} games, predicted image {flash + offset} B of {a.budget} (margin {a.margin})")
    if a.built_dram is not None:
        print(f"  static DRAM {dram_used + dram_offset} B" + (f" of {a.dram}" if a.dram is not None else ""))
    print("  in :", " ".join(sorted(set(chosen.games) - set(current))) or "-")
    print("  out:", " ".join(sorted(set(current) - set(chosen.games))) or "-")
    print("\nbytes per game alone (own sections; shared groups above come on top):")
    for g in sorted(own, key=own.get):
        print(f"  {g:14s} {own[g]:8d} {'in' if g in chosen.games else ''}")
    if a.write:
        _write_list(chosen.games, own)
        print(f"\nwrote {len(chosen.games)} games to {_INI}: build azdelivery-max to confirm the size")


if __name__ == "__main__":
    main(sys.argv[1:])
