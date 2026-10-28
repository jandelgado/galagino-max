import pytest

from romconv.plan import flashpick as fp

S = fp.Section


def _graph():
    # main table (m) names both game roots; a is base-only code; x and y are
    # the games' roots, s is code both games share
    return {
        "m": S(flash=100, dram=10, refs=frozenset({"a", "x", "y"})),
        "a": S(flash=50, dram=0, refs=frozenset()),
        "x": S(flash=300, dram=20, refs=frozenset({"s"})),
        "y": S(flash=500, dram=200, refs=frozenset({"s"})),
        "s": S(flash=70, dram=5, refs=frozenset()),
        "dead": S(flash=999, dram=999, refs=frozenset({"s"})),
    }


def test_reach_follows_references_and_stops_at_boundaries():
    g = _graph()
    assert fp.reach(g, ["m"]) == {"m", "a", "x", "y", "s"}
    assert fp.reach(g, ["m"], stop={"x", "y"}) == {"m", "a"}
    assert fp.reach(g, ["x"]) == {"x", "s"}


def test_groups_split_base_own_and_shared():
    groups = fp.groups(_graph(), ["m"], {"X": ["x"], "Y": ["y"]})
    assert groups.base == (150, 10)
    assert groups.shared == {frozenset({"X"}): (300, 20), frozenset({"Y"}): (500, 200),
                             frozenset({"X", "Y"}): (70, 5)}


def test_pick_counts_shared_bytes_once():
    groups = fp.groups(_graph(), ["m"], {"X": ["x"], "Y": ["y"]})
    assert fp.pick(groups, flash=150 + 300 + 70) == fp.Pick(games=frozenset({"X"}), flash=520, dram=35)
    assert fp.pick(groups, flash=150 + 300 + 500 + 70) == fp.Pick(games=frozenset({"X", "Y"}), flash=1020, dram=235)
    assert fp.pick(groups, flash=150 + 500 + 70 + 10, must={"Y"}).games == frozenset({"Y"})


def test_pick_respects_dram_and_weights():
    groups = fp.groups(_graph(), ["m"], {"X": ["x"], "Y": ["y"]})
    assert fp.pick(groups, flash=10_000, dram=100).games == frozenset({"X"})
    assert fp.pick(groups, flash=150 + 500 + 70, weights={"Y": 5}).games == frozenset({"Y"})


def test_pick_infeasible_must_raises():
    groups = fp.groups(_graph(), ["m"], {"X": ["x"], "Y": ["y"]})
    with pytest.raises(ValueError, match="Y"):
        fp.pick(groups, flash=200, must={"Y"})
