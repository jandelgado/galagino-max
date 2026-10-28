"""Flash bytes per game of linked ESP32 firmware, for picking [games_max].

    python3 tools/flash_table.py src/machines.h .pio/build/<env>/*.elf ...

Reads the firmware symbols (nm) and sums the flash-resident ones
(irom/drom) per game: a symbol belongs to the game whose machine class
name (createMachine<cls> under the GAME_<X> guard of machines.h) starts
it, as "cls::..." (code) or "cls_..." (assets, logos, tables). Shared code
(CPU cores, boards, IDF) stays unattributed and is listed by size, so a
game that alone pulls in a core shows up there. Several ELFs add up: an
env per set of games when the lot cannot link on one ESP32 (drom is
4 MB).
"""
import collections
import os
import re
import subprocess
import sys

_NM = os.path.expanduser("~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-nm")
_IROM = (0x400d0000, 0x40400000)
_DROM = (0x3f400000, 0x3f800000)
_GUARD = re.compile(r"\s*#\s*if(?:def)?\s+(?:defined\()?GAME_([A-Z0-9_]+)")
_CLASS = re.compile(r"createMachine<([A-Za-z0-9_]+)>")
_ANON = "(anonymous namespace)::"


def in_flash(addr):
    return _IROM[0] <= addr < _IROM[1] or _DROM[0] <= addr < _DROM[1]


def classes_by_game(lines):
    """{GAME: {class, ...}} from the machines.h table lines."""
    out = collections.defaultdict(set)
    game = None
    for line in lines:
        m = _GUARD.match(line)
        if m:
            game = m.group(1)
        elif re.match(r"\s*#\s*endif", line):
            game = None
        for cls in _CLASS.findall(line):
            if game:
                out[game].add(cls)
    return dict(out)


def flash_symbols(elf):
    """(size, demangled name) of every flash-resident symbol."""
    out = subprocess.run([_NM, "-S", "-C", "--size-sort", elf], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        p = line.split(None, 3)
        if len(p) == 4 and in_flash(int(p[0], 16)):
            yield int(p[1], 16), p[3]


def attribute(symbols, classes):
    """({GAME: bytes}, [(size, name) unmatched]) over (size, name) symbols."""
    prefixes = [(pre, game) for game, cls in classes.items() for c in cls for pre in (c + "::", c + "_")]
    per = collections.Counter()
    unmatched = []
    for size, name in symbols:
        if name.startswith(_ANON):
            name = name[len(_ANON):]
        game = next((g for pre, g in prefixes if name.startswith(pre)), None)
        if game:
            per[game] += size
        else:
            unmatched.append((size, name))
    return dict(per), unmatched


def main(argv):
    with open(argv[1]) as f:
        classes = classes_by_game(f)
    total = collections.Counter()
    for elf in argv[2:]:
        symbols = list(flash_symbols(elf))
        per, unmatched = attribute(symbols, classes)
        shared = sum(s for s, _ in unmatched)
        print(f"== {elf}: flash symbols {sum(s for s, _ in symbols)}, games {sum(per.values())}, shared {shared}")
        for size, name in sorted(unmatched, reverse=True)[:10]:
            print(f"   shared {size:7d} {name[:80]}")
        total.update(per)
    print("\n== bytes per game, smallest first")
    for game, size in sorted(total.items(), key=lambda kv: kv[1]):
        print(f"{game:14s} {size:8d}")
    missing = sorted(g for g in classes if g not in total)
    if missing:
        print("\nnot in these ELFs:", " ".join(missing))


if __name__ == "__main__":
    main(sys.argv)
