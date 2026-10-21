from romconv import registry
from romconv.core.paths import default
from romconv.remote.catalog import BASES, bundle_dirs, job_name


def test_deps_name_registered_jobs():
    names = {j.name for j in registry.ALL}
    for job in registry.ALL:
        assert set(job.deps) <= names, job.name


def test_ported_games_pull_the_games_their_bundle_holds():
    # a partial run must produce every dir the game's bundle reads
    registered = {j.name for j in registry.ALL}
    checked = 0
    for game in BASES:
        if job_name(game) in registered:
            pulled = {j.name for j in registry.resolve([job_name(game)])}
            assert {job_name(d) for d in bundle_dirs(game)} <= pulled, game
            checked += 1
    assert checked >= 6  # crush eyes lizwiz mrtnt theglob anteater


def test_family_owner_is_a_dep_of_its_games():
    assert "namco54" in registry.by_name("galaga").deps


def test_every_ported_game_has_a_machine_dir():
    machines = default().machines
    for job in registry.ALL:
        if job.machine:
            assert (machines / job.machine).is_dir(), job.name


def test_partial_run_always_includes_utility_jobs_and_catalog():
    names = [j.name for j in registry.resolve(["galaga"])]
    assert {"pokey_poly", "unpack_fixture", "catalog", "bundles", "namco54", "galaga"} <= set(names)
    assert "pacman" not in names
    # a bundles-only partial run too
    assert {"pokey_poly", "unpack_fixture"} <= {j.name for j in registry.resolve(["bundles"])}
