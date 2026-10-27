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
# Not in the model: ARENA_EXTRA (first fit is not linear) and the LZMA
# scratch gap. plan/arena.py checks them on the solution and re-solves with
# a nogood for the rejected (A set, B set).
from dataclasses import dataclass
from typing import Protocol, runtime_checkable


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
    nogoods: tuple[tuple[frozenset[str], frozenset[str]], ...] = ()  # rejected (A set, B set)


@dataclass(frozen=True)
class Solution:
    modes: dict[str, str]        # name -> "PLAIN" | "A" | "B"
    reserve: tuple[int, int]     # sum r4(raw) per block


class Infeasible(ValueError):
    """No assignment satisfies the pins and capacities."""


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


def _reserve(problem: Problem, modes: dict[str, str]) -> tuple[int, int]:
    return (sum(r4(i.raw) for i in problem.items if modes[i.name] == "A"),
            sum(r4(i.raw) for i in problem.items if modes[i.name] == "B"))


class ExhaustiveSolver:
    """DFS over the three choices per item; exact, fine to ~16 items. The
    test oracle for HighsSolver."""

    def solve(self, problem: Problem) -> Solution:
        size_a, size_b = problem.layout
        items = sorted(problem.items, key=lambda i: (-r4(i.raw), i.name))
        nogoods = set(problem.nogoods)
        best: list = [None, None]  # key, modes

        def leaf(modes, sum_a, sum_b):
            in_a = frozenset(n for n, m in modes.items() if m == "A")
            in_b = frozenset(n for n, m in modes.items() if m == "B")
            if (in_a, in_b) in nogoods:
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
