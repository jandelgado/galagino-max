"""Which games fit a flash budget: the [games] max pick as a MILP.

Input is the reference graph of a firmware compiled with every game
(dev/games_max.py reads it from the objects): a Section is one function or
data object with its flash and static RAM bytes and the sections it
references. Closures over that graph give what a game really costs:

  base    reachable from the firmware's own roots (app_main ...) without
          entering a game's roots (createMachine<X>, X::logo): paid always
  shared  every other reached section, keyed by the set of games whose
          closure holds it: a base class, a board, a CPU core, a sample set

pick() chooses the games: maximize their number (or weights) with every
shared group paid once when any of its games is in, flash and static RAM
within their budgets. HiGHS, like plan/solver.py.
"""
import collections
from collections.abc import Iterable, Mapping
from dataclasses import dataclass

import highspy


@dataclass(frozen=True)
class Section:
    flash: int
    dram: int
    refs: frozenset


@dataclass(frozen=True)
class _Groups:
    base: tuple          # (flash, dram) always paid
    shared: dict         # frozenset(games) -> (flash, dram)


@dataclass(frozen=True)
class Pick:
    games: frozenset
    flash: int
    dram: int


def reach(sections: Mapping, roots: Iterable, stop: Iterable = ()) -> set:
    """Sections reachable from roots; a section in stop is never entered."""
    stop = set(stop)
    seen = set()
    todo = [r for r in roots if r in sections]
    while todo:
        key = todo.pop()
        if key in seen:
            continue
        seen.add(key)
        todo.extend(r for r in sections[key].refs if r in sections and r not in seen and r not in stop)
    return seen


def groups(sections: Mapping, base_roots: Iterable, game_roots: Mapping) -> _Groups:
    """Split the graph into the base and the per-game-set shared groups."""
    boundaries = {r for roots in game_roots.values() for r in roots}
    base = reach(sections, base_roots, stop=boundaries)
    owners = collections.defaultdict(set)
    for game, roots in game_roots.items():
        for key in reach(sections, roots) - base:
            owners[key].add(game)
    shared = collections.defaultdict(lambda: [0, 0])
    for key, games in owners.items():
        s = sections[key]
        shared[frozenset(games)][0] += s.flash
        shared[frozenset(games)][1] += s.dram
    return _Groups(base=(sum(sections[k].flash for k in base), sum(sections[k].dram for k in base)),
                  shared={g: tuple(v) for g, v in shared.items()})


def pick(groups: _Groups, flash: int, dram: int | None = None, must: Iterable = (), weights: Mapping = None) -> Pick:
    """The game set of most weight (1 per game by default) within the budgets."""
    weights = dict(weights or {})
    must = set(must)
    games = sorted({g for s in groups.shared for g in s} | must | set(weights))
    h = highspy.Highs()
    h.silent()
    for option, value in (("mip_rel_gap", 0.0), ("mip_abs_gap", 0.0), ("threads", 1), ("random_seed", 0)):
        h.setOptionValue(option, value)
    x = {g: h.addBinary() for g in games}
    for g in must:
        h.changeColBounds(x[g].index, 1, 1)
    y = {}
    for members in groups.shared:
        y[members] = h.addBinary()
        for g in members:
            h.addConstr(y[members] >= x[g])
    used_flash = groups.base[0] + sum(size[0] * y[m] for m, size in groups.shared.items())
    used_dram = groups.base[1] + sum(size[1] * y[m] for m, size in groups.shared.items())
    h.addConstr(used_flash <= flash)
    if dram is not None:
        h.addConstr(used_dram <= dram)

    def solve(objective, sense):
        sense(objective)
        status = h.getModelStatus()
        if status == highspy.HighsModelStatus.kInfeasible:
            raise ValueError("no pick fits: must " + " ".join(sorted(must)))
        if status != highspy.HighsModelStatus.kOptimal:
            raise RuntimeError(f"HiGHS: {status}")
        return h.getObjectiveValue()

    value = sum(weights.get(g, 1) * x[g] for g in games)
    h.addConstr(value == round(solve(value, h.maximize)))
    solve(used_flash, h.minimize)   # among the equal picks the smallest image
    chosen = frozenset(g for g in games if h.val(x[g]) > 0.5)
    paid = [size for m, size in groups.shared.items() if m & chosen]
    return Pick(games=chosen, flash=groups.base[0] + sum(s[0] for s in paid), dram=groups.base[1] + sum(s[1] for s in paid))
