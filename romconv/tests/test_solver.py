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
def test_extras_first_fit_declared_order(solver):
    # extras [100, 60] below the reservation: 100 goes to A, 60 follows into A
    # only if room is left, else B; the split must leave that room
    s = solver.solve(_p([("x", 100, 10)], layout=(200, 59), extras=(100, 60)))
    assert s.modes == {"x": "PLAIN"}, s  # x in A would push the 60 to a B that is too small
    s = solver.solve(_p([("x", 100, 10)], layout=(200, 60), extras=(100, 60)))
    assert s.modes == {"x": "A"}, s  # 100 fills A, 60 lands in B
    s = solver.solve(_p([("x", 100, 10)], layout=(260, 0), extras=(100, 60)))
    assert s.modes == {"x": "A"}, s
    with pytest.raises(Infeasible):
        solver.solve(_p([], layout=(100, 60), extras=(60, 100)))


@pytest.mark.parametrize("solver", SOLVERS)
def test_extras_any_order(solver):
    # [100, 60] fits declared, reversed 60 takes A and 100 strands
    assert solver.solve(_p([], layout=(100, 60), extras=(100, 60))).reserve == (0, 0)
    with pytest.raises(Infeasible):
        solver.solve(_p([], layout=(100, 60), extras=(100, 60), any_order=True))


@pytest.mark.parametrize("solver", SOLVERS)
def test_scratch_gap_after_extras(solver):
    # scratch 50: one block must keep 50 bytes after assets and extras
    p = _p([("x", 100, 10), ("y", 100, 10)], layout=(100, 200), extras=(50,), scratch=50)
    s = solver.solve(p)
    assert sorted(s.modes.values()) == ["A", "B"], s   # extra in B, gap 50 in B (x/y symmetric)
    p = _p([("x", 100, 10), ("y", 100, 10)], layout=(100, 199), extras=(50,), scratch=50)
    s = solver.solve(p)
    assert sorted(s.modes.values()) == ["A", "PLAIN"], s  # one asset gives way
    with pytest.raises(Infeasible):
        solver.solve(_p([("x", 100, 10)], layout=(100, 40), scratch=50, ram=frozenset({"x"})))


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
    layout = (rnd.randrange(60000, 118785), rnd.choice([0, rnd.randrange(30000, 110501)]))  # not 4-aligned
    extras = tuple(rnd.randrange(4, 50000, 4) for _ in range(rnd.randint(0, 3)))
    return Problem(tuple(items), layout, frozenset(flash), frozenset(ram), extras, rnd.random() < 0.5, 5232)


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


def test_67_items_solve_fast():
    # 27 assets (marble) plus 40 chunks of a big ROM
    rnd = random.Random(2)
    items = [Item(f"a{i:02d}", rnd.randrange(2048, 65536), rnd.randrange(1024, 40000), 1) for i in range(27)]
    items += [Item(f"chunk{i:02d}", 65536, rnd.randrange(40000, 60000), 1) for i in range(40)]
    t0 = time.perf_counter()
    HighsSolver().solve(Problem(tuple(items), (118784, 110500), extras=(10440, 57344, 39904), scratch=5232))
    assert time.perf_counter() - t0 < 5.0   # ~0.2 s: catches a blowup, not a loaded host


def test_sort_key():
    p = _p([("b", 100, 40), ("a", 100, 40)])
    assert sort_key(p, {"a": "A", "b": "PLAIN"}) == (140, 100, (0, 1), 0)
    assert sort_key(p, {"a": "B", "b": "PLAIN"}) == (140, 100, (0, 1), 100)
    assert r4(5) == 8 and r4(8) == 8


@pytest.mark.parametrize("solver", SOLVERS)
def test_unaligned_layout_matches_first_fit(solver):
    # A not a multiple of 4: the extra (100) does not fit A = 99 and lands in B
    s = solver.solve(_p([("f", 100, 10)], layout=(99, 200), extras=(100,), flash=frozenset({"f"})))
    assert s.modes == {"f": "PLAIN"} and s.reserve == (0, 0)
    s = solver.solve(_p([("x", 96, 10)], layout=(98, 200), extras=(100,)))
    assert s.modes == {"x": "A"}


def test_name_tie_break_beyond_rank_28():
    # 30 equal items, 28 pinned to flash, room for one of a28/a29: name order says a28
    items = tuple(Item(f"a{i:02d}", 100, 50, 1) for i in range(30))
    p = Problem(items, (100, 0), flash=frozenset(i.name for i in items[:28]))
    hs, ex = HighsSolver().solve(p), ExhaustiveSolver().solve(p)
    assert hs.modes["a28"] == "A" and hs.modes["a29"] == "PLAIN"
    assert sort_key(p, hs.modes) == sort_key(p, ex.modes)
