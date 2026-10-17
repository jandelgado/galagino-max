#!/usr/bin/env python3
# Arena split of one game per layout: which assets stay PLAIN (flash) and
# which block each COMPRESSED asset unpacks into. Writes <game>_arena_plan.h,
# read by the planned asset headers (asset_emit.py, ROMCONV_PLAN) and
# machines.h.
#
# Inputs: `// arena-asset <name> <bytes> <align> <hint> <type>` markers in the game's
# generated headers, ARENA_EXTRA / ARENA_RAM / ARENA_FLASH in its machine
# header (plain literals), block sizes from arena_layout.h. Extras (non-asset
# Arena::alloc) must fit first-fit in every order.
#
# Usage (from romconv/): arena_plan.py <game>
import glob
import itertools
import os
import re
import sys

from arena_layout import LAYOUTS

MACHINES = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "..", "..", "source", "src", "machines")


def _r4(n):
    return (n + 3) & ~3


# extras checked in every order up to this count, declared order above
ANY_ORDER_MAX = 8


def _extras_fit(extras, free_a, free_b):
    """First fit in run-time order, A then B, like Arena::alloc."""
    used_a = used_b = 0
    for e in extras:
        if used_a + e <= free_a:
            used_a += e
        elif used_b + e <= free_b:
            used_b += e
        else:
            return False
    return True


def solve(assets, extras, layout, ram=(), flash=()):
    """Mode per asset ("PLAIN", "A", "B") and reserved bytes per block.

    assets: (name, bytes, align[, hint]); hint "PLAIN"/"COMPRESSED" is the
    converter's own mode. Minimizes PLAIN bytes, then modes that differ from
    the hint, then PLAIN asset count, then the sorted PLAIN names.
    ValueError if no split fits.
    """
    names = {a[0] for a in assets}
    hint_plain = {a[0] for a in assets if len(a) > 3 and a[3] == "PLAIN"}
    for pin in list(ram) + list(flash):
        if pin not in names:
            raise ValueError(f"pinned asset {pin} not in the game's assets")
    size_a, size_b = layout
    items = sorted(((_r4(a[1]), a[0]) for a in assets), reverse=True)
    extras4 = [_r4(e) for e in extras]
    extra_total = sum(extras4)
    # run-time order is not always fixed (Arena::alloc as function arguments)
    if len(extras4) <= ANY_ORDER_MAX:
        orders = set(itertools.permutations(extras4))
    else:
        orders = {tuple(extras4)}
    fits = {}

    def extras_fit(free_a, free_b):
        if (free_a, free_b) not in fits:
            fits[(free_a, free_b)] = all(_extras_fit(o, free_a, free_b) for o in orders)
        return fits[(free_a, free_b)]
    best = [None, None]  # key, modes

    def key(modes):
        plain = sorted(n for n, m in modes.items() if m == "PLAIN")
        changed = len(set(plain) ^ hint_plain)
        return (sum(s for s, n in items if modes[n] == "PLAIN"), changed, len(plain), plain)

    def dfs(i, sum_a, sum_b, plain_bytes, plain_count, modes):
        if best[0] is not None and plain_bytes > best[0][0]:
            return
        if extra_total > (size_a - sum_a) + (size_b - sum_b):
            return
        if i == len(items):
            if extras_fit(size_a - sum_a, size_b - sum_b):
                k = key(modes)
                if best[0] is None or k < best[0]:
                    best[0], best[1] = k, dict(modes)
            return
        s, n = items[i]
        if n not in flash:
            if sum_a + s <= size_a:
                modes[n] = "A"
                dfs(i + 1, sum_a + s, sum_b, plain_bytes, plain_count, modes)
            if sum_b + s <= size_b:
                modes[n] = "B"
                dfs(i + 1, sum_a, sum_b + s, plain_bytes, plain_count, modes)
        if n not in ram:
            modes[n] = "PLAIN"
            dfs(i + 1, sum_a, sum_b, plain_bytes + s, plain_count + 1, modes)
        modes.pop(n, None)

    dfs(0, 0, 0, 0, 0, {})
    if best[1] is None:
        raise ValueError(f"does not fit layout {size_a}+{size_b}")
    modes = best[1]
    reserve = (sum(s for s, n in items if modes[n] == "A"),
               sum(s for s, n in items if modes[n] == "B"))
    return modes, reserve


def _array(text, name):
    m = re.search(r"\b" + name + r"\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        return []
    return [v.strip() for v in re.sub(r"//[^\n]*", "", m.group(1)).split(",") if v.strip()]


def parse_machine_header(path):
    """(extras, ram, flash) from the ARENA_* arrays; literals only."""
    with open(path) as f:
        text = f.read()
    extras = []
    for v in _array(text, "ARENA_EXTRA"):
        if not re.fullmatch(r"\d+", v):
            raise ValueError(f"{path}: ARENA_EXTRA entry '{v}' is not an integer literal")
        extras.append(int(v))
    pins = []
    for name in ("ARENA_RAM", "ARENA_FLASH"):
        vals = []
        for v in _array(text, name):
            if not re.fullmatch(r'"\w+"', v):
                raise ValueError(f"{path}: {name} entry {v} is not a string literal")
            vals.append(v.strip('"'))
        pins.append(vals)
    return extras, pins[0], pins[1]


def parse_markers(game_dir):
    """[(name, bytes, align, hint, type)] of the planned assets in a machine dir."""
    assets = []
    for path in sorted(glob.glob(os.path.join(game_dir, "*.h"))):
        if path.endswith("_arena_plan.h"):
            continue
        with open(path) as f:
            for m in re.finditer(r"^// arena-asset (\w+) (\d+) (\d+) (PLAIN|COMPRESSED) (.+)$", f.read(), re.M):
                assets.append((m.group(1), int(m.group(2)), int(m.group(3)), m.group(4), m.group(5)))
    return assets


def _ident(game):
    return game if game[0].isalpha() else "_" + game


def plan_header(game, plans, types):
    """Header text; plans = {"local": (modes, reserve), "remote": (...)},
    types = {name: element type}, for the <name>_asset aliases."""
    ident = _ident(game)
    guard = ident.upper() + "_ARENA_PLAN_H"
    out = [f"// Generated by romconv arena_plan.py: Arena split of {game} per layout. Do not edit.",
           f"#ifndef {guard}", f"#define {guard}", '#include "../../emulation/asset.h"']
    for cond, layout in (("#ifdef ASSETS_REMOTE", "remote"), ("#else", "local")):
        modes, reserve = plans[layout]
        a, b = LAYOUTS[layout]
        out.append(cond)
        out.append(f"static_assert(Arena::BLOCK_A_SIZE == {a} && Arena::BLOCK_B_SIZE == {b}, "
                   '"Arena layout changed: rerun romconv");')
        for name in sorted(modes):
            mode = modes[name]
            out.append(f"#define {name}_PLAIN {1 if mode == 'PLAIN' else 0}")
            out.append(f"#define {name}_BLOCK " + {"PLAIN": "ARENA_ANY", "A": "ARENA_A", "B": "ARENA_B"}[mode])
            out.append(f"using {name}_asset = Asset<{types[name]}, {'PLAIN' if mode == 'PLAIN' else 'COMPRESSED'}>;")
        out.append(f"static constexpr ArenaPlan {ident}_arena_plan = {{ {reserve[0]}, {reserve[1]} }};")
    out += ["#endif", "#endif", ""]
    return "\n".join(out)


def main(game):
    game_dir = os.path.normpath(os.path.join(MACHINES, game))
    assets = parse_markers(game_dir)
    if not assets:
        raise ValueError(f"{game}: no planned assets in {game_dir} (ROMCONV_PLAN not set?)")
    extras, ram, flash = parse_machine_header(os.path.join(game_dir, game + ".h"))
    plans = {}
    for layout, (a, b) in LAYOUTS.items():
        try:
            modes, reserve = solve(assets, extras, (a, b), ram, flash)
        except ValueError as e:
            raise ValueError(f"{game}: {e}")
        plans[layout] = (modes, reserve)
        plain = [n for n in sorted(modes) if modes[n] == "PLAIN"]
        plain_bytes = sum(a[1] for a in assets if modes[a[0]] == "PLAIN")
        print(f"  {game} {layout:6s} A {reserve[0]:6d} B {reserve[1]:6d} reserved of {a}+{b},"
              f" extras {sum(map(_r4, extras))}, PLAIN {plain_bytes} B: {' '.join(plain) or '-'}")
    with open(os.path.join(game_dir, game + "_arena_plan.h"), "w") as f:
        f.write(plan_header(game, plans, {a[0]: a[4] for a in assets}))


if __name__ == "__main__":
    try:
        main(sys.argv[1])
    except ValueError as e:
        print(f"ERROR: {e}")
        sys.exit(1)
