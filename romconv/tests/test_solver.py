import random
import time

import pytest

from romconv.plan.solver import (ExhaustiveSolver, HighsSolver, Infeasible, Item, Problem, Solution, Solver, r4,
                                 sort_key)

SOLVERS = [ExhaustiveSolver(), HighsSolver()]


def _p(items, layout=(118784, 110500), **kw):
    return Problem(tuple(Item(n, raw, packed, 1) for n, raw, packed in items), layout, **kw)


@pytest.mark.parametrize("solver", SOLVERS)
def test_solvers_implement_the_protocol(solver):
    assert isinstance(solver, Solver)
    assert isinstance(solver.solve(_p([("a", 8, 4)])), Solution)


@pytest.mark.parametrize("solver", SOLVERS)
def test_compressible_goes_to_ram(solver):
    s = solver.solve(_p([("a", 10000, 4000)]))
    assert s.modes["a"] in ("A", "B") and s.reserve in ((10000, 0), (0, 10000))


@pytest.mark.parametrize("solver", SOLVERS)
def test_incompressible_stays_plain(solver):
    # packed >= raw: the Arena copy would not save a flash byte
    s = solver.solve(_p([("a", 10000, 10000), ("b", 10000, 10001)]))
    assert s.modes == {"a": "PLAIN", "b": "PLAIN"} and s.reserve == (0, 0)


@pytest.mark.parametrize("solver", SOLVERS)
def test_flash_bytes_beat_count(solver):
    # one big saving beats two small ones that would take the same block
    s = solver.solve(_p([("big", 100, 10), ("s1", 60, 40), ("s2", 60, 40)], layout=(100, 0)))
    assert s.modes["big"] == "A" and s.modes["s1"] == s.modes["s2"] == "PLAIN"


@pytest.mark.parametrize("solver", SOLVERS)
def test_pins(solver):
    p = _p([("a", 100, 10), ("b", 100, 100)], flash=frozenset({"a"}), ram=frozenset({"b"}))
    s = solver.solve(p)
    assert s.modes["a"] == "PLAIN" and s.modes["b"] != "PLAIN"


@pytest.mark.parametrize("solver", SOLVERS)
def test_ram_pin_too_big_is_infeasible(solver):
    with pytest.raises(Infeasible):
        solver.solve(_p([("a", 200, 10)], layout=(100, 50), ram=frozenset({"a"})))


@pytest.mark.parametrize("solver", SOLVERS)
def test_b_zero_never_assigns_b(solver):
    s = solver.solve(_p([("a", 2000, 1), ("b", 2000, 1)], layout=(3000, 0)))
    assert sorted(s.modes.values()) == ["A", "PLAIN"] and s.reserve == (2000, 0)


@pytest.mark.parametrize("solver", SOLVERS)
def test_nogood_excludes_that_assignment(solver):
    p = _p([("a", 100, 10)], layout=(100, 100))
    first = solver.solve(p)
    block = next(iter(first.modes.values()))
    bad = (frozenset({"a"}), frozenset()) if block == "A" else (frozenset(), frozenset({"a"}))
    second = solver.solve(Problem(p.items, p.layout, nogoods=(bad,)))
    assert second.modes["a"] not in ("PLAIN", block)


@pytest.mark.parametrize("solver", SOLVERS)
def test_tie_break_fewer_compressed_bytes_then_names(solver):
    # x and y save the same flash; only one fits: fewer COMPRESSED raw bytes wins
    s = solver.solve(_p([("x", 100, 50), ("y", 150, 100)], layout=(150, 0)))
    assert s.modes == {"x": "A", "y": "PLAIN"}
    # equal everything: PLAIN flags in name order, smallest tuple: "a" in RAM
    s = solver.solve(_p([("a", 100, 50), ("b", 100, 50)], layout=(100, 0)))
    assert s.modes == {"a": "A", "b": "PLAIN"}


@pytest.mark.parametrize("solver", SOLVERS)
def test_reserve_rounded_to_4(solver):
    s = solver.solve(_p([("x", 5, 1)], layout=(8, 0)))
    assert s.reserve == (8, 0)
    with pytest.raises(Infeasible):
        solver.solve(_p([("x", 5, 1)], layout=(7, 0), ram=frozenset({"x"})))


def _random_problem(rnd, n):
    items, flash, ram = [], set(), set()
    for i in range(n):
        raw = rnd.choice([1024, 4096, 8192, 16384, 32768, 65536]) + rnd.randrange(4096)
        packed = int(raw * rnd.uniform(0.3, 1.05))
        items.append(Item(f"a{i:02d}", raw, packed, 1))
        r = rnd.random()
        if r < 0.1:
            flash.add(items[-1].name)
        elif r < 0.2 and raw <= 60000:
            ram.add(items[-1].name)
    layout = (rnd.randrange(60000, 118785, 4), rnd.choice([0, rnd.randrange(30000, 110501, 4)]))
    return Problem(tuple(items), layout, frozenset(flash), frozenset(ram))


def test_highs_matches_exhaustive():
    # 200 random instances, same key (and so the same PLAIN set)
    rnd = random.Random(1)
    for _ in range(200):
        p = _random_problem(rnd, rnd.randint(1, 12))
        try:
            ex = ExhaustiveSolver().solve(p)
        except Infeasible:
            with pytest.raises(Infeasible):
                HighsSolver().solve(p)
            continue
        hs = HighsSolver().solve(p)
        assert sort_key(p, hs.modes) == sort_key(p, ex.modes), p


def test_67_items_under_one_second():
    # 27 assets (marble) plus 40 chunks of a big ROM (step 3)
    rnd = random.Random(2)
    items = [Item(f"a{i:02d}", rnd.randrange(2048, 65536), rnd.randrange(1024, 40000), 1) for i in range(27)]
    items += [Item(f"chunk{i:02d}", 65536, rnd.randrange(40000, 60000), 1) for i in range(40)]
    t0 = time.perf_counter()
    HighsSolver().solve(Problem(tuple(items), (118784, 110500)))
    assert time.perf_counter() - t0 < 1.0


def test_sort_key():
    p = _p([("b", 100, 40), ("a", 100, 40)])
    assert sort_key(p, {"a": "A", "b": "PLAIN"}) == (140, 100, (0, 1), 0)
    assert sort_key(p, {"a": "B", "b": "PLAIN"}) == (140, 100, (0, 1), 100)
    assert r4(5) == 8 and r4(8) == 8
