# The Arena split of one game and layout as an optimization problem.
#
# Every planned asset ends up PLAIN (raw bytes in flash) or unpacked into
# Arena block A or B (LZMA bytes in flash). Two-bin multiple knapsack with
# a reject option: minimize flash bytes
#
#     sum(raw for PLAIN) + sum(packed for COMPRESSED)
#
# under the block capacities (sum r4(raw) per block). Ties: fewer COMPRESSED
# raw bytes (less unpack time at start), then the PLAIN flags in name order
# (smallest tuple: earlier names in RAM), then fewer bytes in B (A first,
# as first fit does); together they make the output deterministic for golden
# diffs. Both solvers return the same sort_key().
#
# Below the reservations the machine's extras (ARENA_EXTRA, other
# Arena::alloc calls) run first fit in declared order, A then B, or in every
# order with ARENA_EXTRA_ANY_ORDER; after them one block must keep `scratch`
# bytes for the LZMA probability table. Both are part of the problem: the
# exhaustive solver checks them at every leaf, the MILP encodes first fit
# with one binary per extra and order (big-M on "fits in A").
import itertools
from dataclasses import dataclass
from typing import Protocol, runtime_checkable

import highspy


def r4(n: int) -> int:
    """Round up to 4: Arena::alloc granularity of the reservations."""
    return (n + 3) & ~3


@dataclass(frozen=True)
class Item:
    name: str
    raw: int      # unpacked bytes
    packed: int   # LZMA bytes
    align: int


@dataclass(frozen=True)
class Problem:
    items: tuple[Item, ...]
    layout: tuple[int, int]                 # (A, B) bytes, B may be 0
    flash: frozenset[str] = frozenset()     # ARENA_FLASH pins: PLAIN
    ram: frozenset[str] = frozenset()       # ARENA_RAM pins: A or B
    extras: tuple[int, ...] = ()            # ARENA_EXTRA bytes (r4), declared order
    any_order: bool = False                 # ARENA_EXTRA_ANY_ORDER
    scratch: int = 0                        # gap one block keeps after the extras


@dataclass(frozen=True)
class Solution:
    modes: dict[str, str]        # name -> "PLAIN" | "A" | "B"
    reserve: tuple[int, int]     # sum r4(raw) per block


class Infeasible(ValueError):
    """No assignment satisfies the pins, capacities, extras and scratch."""


@runtime_checkable
class Solver(Protocol):
    def solve(self, problem: Problem) -> Solution: ...


def sort_key(problem: Problem, modes: dict[str, str]) -> tuple[int, int, tuple[int, ...], int]:
    """(flash bytes, COMPRESSED raw bytes, PLAIN flags in name order, r4 bytes in B)."""
    flash = sum(i.raw if modes[i.name] == "PLAIN" else i.packed for i in problem.items)
    compressed = sum(i.raw for i in problem.items if modes[i.name] != "PLAIN")
    flags = tuple(1 if modes[i.name] == "PLAIN" else 0 for i in sorted(problem.items, key=lambda i: i.name))
    in_b = sum(r4(i.raw) for i in problem.items if modes[i.name] == "B")
    return flash, compressed, flags, in_b


def _orders(problem: Problem) -> list[tuple[int, ...]]:
    """The extras orders a split must survive: the declared one, or every one."""
    if problem.any_order:
        return sorted(set(itertools.permutations(problem.extras)))
    return [tuple(problem.extras)]


def _first_fit(extras, free_a, free_b):
    """Bytes used per block by a first fit in run-time order, A then B, like
    Arena::alloc; None if an extra fits nowhere."""
    used_a = used_b = 0
    for e in extras:
        if used_a + e <= free_a:
            used_a += e
        elif used_b + e <= free_b:
            used_b += e
        else:
            return None
    return used_a, used_b


def _extras_fit(problem: Problem, free_a: int, free_b: int) -> bool:
    """Every order fits below the reservations and leaves the scratch gap in one block."""
    for order in _orders(problem):
        used = _first_fit(order, free_a, free_b)
        if used is None or max(free_a - used[0], free_b - used[1]) < problem.scratch:
            return False
    return True


def _reserve(problem: Problem, modes: dict[str, str]) -> tuple[int, int]:
    return (sum(r4(i.raw) for i in problem.items if modes[i.name] == "A"),
            sum(r4(i.raw) for i in problem.items if modes[i.name] == "B"))


class ExhaustiveSolver:
    """DFS over the three choices per item; exact, fine to ~16 items. The
    test oracle for HighsSolver."""

    def solve(self, problem: Problem) -> Solution:
        size_a, size_b = problem.layout
        items = sorted(problem.items, key=lambda i: (-r4(i.raw), i.name))
        fits = {}  # free (a, b) -> extras_fit
        best: list = [None, None]  # key, modes

        def leaf(modes, sum_a, sum_b):
            free = (size_a - sum_a, size_b - sum_b)
            if free not in fits:
                fits[free] = _extras_fit(problem, *free)
            if not fits[free]:
                return
            key = sort_key(problem, modes)
            if best[0] is None or key < best[0]:
                best[0], best[1] = key, dict(modes)

        def dfs(i, sum_a, sum_b, flash, modes):
            if best[0] is not None and flash > best[0][0]:
                return
            if i == len(items):
                leaf(modes, sum_a, sum_b)
                return
            item = items[i]
            size = r4(item.raw)
            if item.name not in problem.flash:
                if sum_a + size <= size_a:
                    modes[item.name] = "A"
                    dfs(i + 1, sum_a + size, sum_b, flash + item.packed, modes)
                if sum_b + size <= size_b:
                    modes[item.name] = "B"
                    dfs(i + 1, sum_a, sum_b + size, flash + item.packed, modes)
            if item.name not in problem.ram:
                modes[item.name] = "PLAIN"
                dfs(i + 1, sum_a, sum_b, flash + item.raw, modes)
            modes.pop(item.name, None)

        dfs(0, 0, 0, 0, {})
        if best[1] is None:
            raise Infeasible(f"does not fit layout {size_a}+{size_b}")
        return Solution(best[1], _reserve(problem, best[1]))


# PLAIN flags beyond this name rank share one weight: 2^-53 is the last
# exact step below 1.0 in a double (ties that deep are left to the solver)
_NAME_RANK_MAX = 52


class HighsSolver:
    """MILP via highspy (HiGHS): binaries a_i, b_i per item, one solve per
    sort_key() term, each fixing the previous optimum. Exact: integrality
    gap 0, integer objectives in the byte passes."""

    def solve(self, problem: Problem) -> Solution:
        size_a, size_b = problem.layout
        items = problem.items
        if not items:
            # nothing to place: only the extras and the scratch decide
            if not _extras_fit(problem, size_a, size_b):
                raise Infeasible(f"does not fit layout {size_a}+{size_b}")
            return Solution({}, (0, 0))
        h = highspy.Highs()
        h.silent()
        for option, value in (("mip_rel_gap", 0.0), ("mip_abs_gap", 0.0), ("threads", 1), ("random_seed", 0)):
            h.setOptionValue(option, value)

        a = [h.addBinary() for _ in items]
        b = [h.addBinary() for _ in items]
        for i, item in enumerate(items):
            if item.name in problem.flash:
                h.changeColBounds(a[i].index, 0, 0)
                h.changeColBounds(b[i].index, 0, 0)
            elif item.name in problem.ram:
                h.addConstr(a[i] + b[i] == 1)
            else:
                h.addConstr(a[i] + b[i] <= 1)
            if size_b == 0:
                h.changeColBounds(b[i].index, 0, 0)
        free_a = size_a - sum(r4(it.raw) * a[i] for i, it in enumerate(items))
        free_b = size_b - sum(r4(it.raw) * b[i] for i, it in enumerate(items))
        h.addConstr(free_a >= 0)
        h.addConstr(free_b >= 0)
        self._extras(h, problem, free_a, free_b)

        def run(objective):
            h.minimize(objective)
            status = h.getModelStatus()
            if status == highspy.HighsModelStatus.kInfeasible:
                raise Infeasible(f"does not fit layout {size_a}+{size_b}")
            if status != highspy.HighsModelStatus.kOptimal:
                raise RuntimeError(f"HiGHS: {status}")
            return h.getObjectiveValue()

        flash = sum(it.raw for it in items) - sum((it.raw - it.packed) * (a[i] + b[i]) for i, it in enumerate(items))
        h.addConstr(flash == round(run(flash)))
        compressed = sum(it.raw * (a[i] + b[i]) for i, it in enumerate(items))
        h.addConstr(compressed == round(run(compressed)))

        rank = {name: r for r, name in enumerate(sorted(it.name for it in items))}
        run(sum(2.0 ** -(min(rank[it.name], _NAME_RANK_MAX) + 1) * (1 - a[i] - b[i]) for i, it in enumerate(items)))
        for i in range(len(items)):
            if round(h.val(a[i]) + h.val(b[i])):
                h.addConstr(a[i] + b[i] == 1)
            else:
                h.changeColBounds(a[i].index, 0, 0)
                h.changeColBounds(b[i].index, 0, 0)
        run(sum(r4(it.raw) * b[i] for i, it in enumerate(items)))

        modes = {}
        for i, item in enumerate(items):
            modes[item.name] = "A" if round(h.val(a[i])) else "B" if round(h.val(b[i])) else "PLAIN"
        return Solution(modes, _reserve(problem, modes))

    @staticmethod
    def _extras(h, problem, free_a, free_b):
        """First fit of the extras per order: x_k = 1 iff extra k still fits
        in A (big-M), the rest must fit B; then one block keeps the scratch."""
        size_a, size_b = problem.layout
        big = size_a + size_b + sum(problem.extras) + 4
        for order in _orders(problem):
            x = [h.addBinary() for _ in order]
            used_a = 0
            for k, e in enumerate(order):
                room = free_a - used_a - e  # >= 0: fits in A
                h.addConstr(room >= -big * (1 - x[k]))
                h.addConstr(room <= -4 + big * x[k])
                used_a = used_a + e * x[k]
            used_b = sum(e * (1 - x[k]) for k, e in enumerate(order))
            h.addConstr(free_b - used_b >= 0)
            # s = 1: the gap is in A
            s = h.addBinary()
            h.addConstr(free_a - used_a >= problem.scratch * s)
            h.addConstr(free_b - used_b >= problem.scratch * (1 - s))
