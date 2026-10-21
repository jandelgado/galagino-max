# Arena split of one game per layout: which assets stay PLAIN (flash) and
# which block each COMPRESSED asset unpacks into. Writes <game>_arena_plan.h,
# read by the planned asset headers (emit.asset) and machines.h.
#
# Inputs: the planned assets in the job's manifest, ARENA_EXTRA / ARENA_RAM /
# ARENA_FLASH in its machine header (plain literals), block sizes from
# arena_layout.h. Extras (non-asset Arena::alloc) run first-fit in declared
# order, or must fit in every order with ARENA_EXTRA_ANY_ORDER (allocations
# as function arguments).
import itertools
import re
from collections.abc import Sequence
from pathlib import Path

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.manifest import Asset
from romconv.emit.text import WIDTH
from romconv.plan.layout import header_path, read_layouts
from romconv.remote.catalog import c_symbol


def _r4(n):
    return (n + 3) & ~3


# any_order: extras checked in every order up to this count
_ANY_ORDER_MAX = 8


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


def solve(assets, extras, layout, ram=(), flash=(), any_order=False):
    """Mode per asset ("PLAIN", "A", "B") and reserved bytes per block.

    assets: (name, bytes, align[, hint]); hint "PLAIN"/"COMPRESSED" is the
    converter's own mode. The converter's split (pins applied) wins when it
    fits: hand-tuned for the local layout, so mode 1 stays as it was.
    Otherwise: fewest PLAIN bytes, then modes that differ from the hint,
    then PLAIN asset count, then the sorted PLAIN names. any_order: extras
    must fit in every run-time order. ValueError if no split fits.
    """
    names = {a[0] for a in assets}
    hint_plain = {a[0] for a in assets if len(a) > 3 and a[3] == "PLAIN"}
    for pin in list(ram) + list(flash):
        if pin not in names:
            raise ValueError(f"pinned asset {pin} not in the game's assets")
    if all(len(a) > 3 for a in assets):
        plain = (hint_plain | set(flash)) - set(ram)
        try:
            return _search(assets, extras, layout, names - plain, plain, hint_plain, any_order)
        except ValueError:
            pass
    return _search(assets, extras, layout, ram, flash, hint_plain, any_order)


def _search(assets, extras, layout, ram, flash, hint_plain, any_order):
    size_a, size_b = layout
    items = sorted(((_r4(a[1]), a[0]) for a in assets), reverse=True)
    extras4 = [_r4(e) for e in extras]
    extra_total = sum(extras4)
    if any_order:
        if len(extras4) > _ANY_ORDER_MAX:
            raise ValueError(f"ARENA_EXTRA_ANY_ORDER with more than {_ANY_ORDER_MAX} extras")
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


def parse_machine_header(path: Path):
    """(extras, ram, flash, any_order) from the ARENA_* constants; literals only."""
    text = Path(path).read_text()
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
    any_order = bool(re.search(r"\bARENA_EXTRA_ANY_ORDER\s*=\s*true\b", text))
    return extras, pins[0], pins[1], any_order


def extras_key(extras, any_order):
    """Same as arena.h arenaExtrasKey(): the plan records what it was solved for."""
    key = 0
    for e in extras:
        key = (key * 31 + e) & 0xFFFFFFFF
    return key ^ 0x80000000 if any_order else key


def plan_header(game: str, plans: dict, types: dict[str, str], key: int,
                layouts: dict[str, tuple[int, int]]) -> str:
    """Header text; plans = {"local": (modes, reserve), "remote": (...)},
    types = {name: element type}, for the <name>_asset aliases, key =
    extras_key() of the machine's ARENA_EXTRA, layouts = read_layouts()."""
    ident = c_symbol(game)
    guard = ident.upper() + "_ARENA_PLAN_H"
    out = [f"// Generated by romconv arena_plan.py: Arena split of {game} per layout. Do not edit.",
           f"#ifndef {guard}", f"#define {guard}", '#include "../../emulation/asset.h"',
           f"static constexpr uint32_t {ident}_arena_extras_key = {key}u;"]
    for cond, layout in (("#ifdef ASSETS_REMOTE", "remote"), ("#else", "local")):
        modes, reserve = plans[layout]
        a, b = layouts[layout]
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


def plan_assets(assets: Sequence[Asset]) -> list[tuple[str, int, int, str, str]]:
    """[(name, bytes, align, hint, type)] of the planned assets."""
    return [(a.name, len(a.raw), WIDTH[a.ctype], a.hint, f"{a.ctype}{a.inner}")
            for a in assets if a.mode == "PLANNED"]


def write_arena_plan(ctx: Context) -> None:
    game = ctx.job.name
    assets = plan_assets(ctx.manifest.assets())
    machine_dir = ctx.path(game + ".h").parent
    if not assets:
        raise ConversionError(f"{game}: no planned assets in {machine_dir}")
    try:
        layouts = read_layouts(header_path(ctx.paths))
        extras, ram, flash, any_order = parse_machine_header(ctx.path(game + ".h"))
        plans = {}
        for layout, (a, b) in layouts.items():
            try:
                modes, reserve = solve(assets, extras, (a, b), ram, flash, any_order)
            except ValueError as e:
                raise ValueError(f"{game}: {e}")
            plans[layout] = (modes, reserve)
            plain = [n for n in sorted(modes) if modes[n] == "PLAIN"]
            plain_bytes = sum(x[1] for x in assets if modes[x[0]] == "PLAIN")
            ctx.log.info(f"  {game} {layout:6s} A {reserve[0]:6d} B {reserve[1]:6d} reserved of {a}+{b},"
                         f" extras {sum(map(_r4, extras))}, PLAIN {plain_bytes} B: {' '.join(plain) or '-'}")
        text = plan_header(game, plans, {x[0]: x[4] for x in assets}, extras_key(extras, any_order), layouts)
    except ValueError as e:
        raise ConversionError(str(e))
    with ctx.header(game + "_arena_plan.h", plan=False, remote=False) as f:
        f.write(text)
