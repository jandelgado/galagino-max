# Game catalog of mode 3 (ASSETS_REMOTE) builds and the macros that gate code
# and assets, written to source/src/catalog.h:
#
#   GAME_<X>       machine code compiled in: ENABLE_<X>, or any catalog game
#                  in an ASSETS_REMOTE build
#   BUILTIN_<DIR>  assets of machines/<dir>/ in the app: ENABLE_ of a game
#                  whose class hierarchy constructs them (mspacman: pacman)
#
# Games come from machines.h, which is tracked (logo and ROM headers are not).
import re
from types import MappingProxyType

from romconv.core.context import Context, SHARED_DIRS
from romconv.core.job import Job
from romconv.core.paths import Paths
from romconv.emit.write import banner

# broken or WiP, not in remote builds
EXCLUDED = frozenset({"motorace"})

# catalog games without asset blobs (tables only): always built in
NO_BLOBS = frozenset({"hero"})

# games whose job (command line) name differs
_JOB_NAMES = MappingProxyType({"burgertime": "btime", "spaceinvaders": "invaders"})

# game -> base dirs whose assets its class hierarchy also constructs, nearest
# first (base class ctors construct their Asset members)
BASES = MappingProxyType({
    **{g: ("pacman",) for g in
       ("alibaba", "crush", "eyes", "lizwiz", "mrtnt", "mspacman", "pengo", "theglob")},
    "turtles": ("scramble",),
    "supercobra": ("scramble",),
    "amidar": ("turtles", "scramble"),
    "anteater": ("frogger",),
    "baluba": ("starforce",),
    "bnj": ("burgertime",),
    "dkongjr": ("dkong",),
    "mrgoemon": ("gberet",),
    "sxevious": ("xevious",),
    "szaxxon": ("zaxxon",),
})


def read_games(paths: Paths) -> dict[str, bool]:
    """Every game of machines.h -> whether it is in the catalog."""
    text = (paths.src / "machines.h").read_text(encoding="utf-8")
    games = {g.lower() for g in re.findall(r"#ifdef (?:ENABLE|GAME)_(\w+)", text)}
    return {g: g not in EXCLUDED for g in sorted(games)}


def bundle_dirs(game: str) -> tuple[str, ...]:
    """Machine dirs whose assets the game's class hierarchy constructs."""
    return (game, *BASES.get(game, ()))


def job_name(game: str) -> str:
    """Name of the job that converts the game."""
    return _JOB_NAMES.get(game, game)


def _guard(names, remote=False):
    conds = [f"defined(ENABLE_{n.upper()})" for n in names]
    if remote:
        conds.append("defined(ASSETS_REMOTE)")
    return "#if " + " || ".join(conds)


def catalog_h(games: dict[str, bool]) -> str:
    out = [banner(__file__).rstrip("\n"),
           "#ifndef CATALOG_H", "#define CATALOG_H", "", "// machine code compiled in"]
    for g in sorted(games):
        out += [_guard([g], games[g]), f"#define GAME_{g.upper()}", "#endif"]
    out += ["", "// assets in the app, not in a remote bundle"]
    for g in sorted(games):
        users = [g] + sorted(sub for sub, bases in BASES.items() if g in bases)
        out += [_guard(users), f"#define BUILTIN_{g.upper()}", "#endif"]
    out += [f"#define BUILTIN_{d.upper()}" for d in sorted(SHARED_DIRS)]
    out += ["", "#endif", ""]
    return "\n".join(out)


def _convert(ctx: Context) -> None:
    text = catalog_h(read_games(ctx.paths))
    ctx.paths.src.joinpath("catalog.h").write_text(text, encoding="utf-8", newline="\n")


JOB = Job(name="catalog", convert=_convert, phase=2)
