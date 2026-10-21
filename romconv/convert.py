#!/usr/bin/env -S uv run
import sys

from internal.pyconv.convutil import fatal, info, run
from romconv import registry
from romconv.core import runner
from romconv.core.errors import ConversionError
from romconv.remote.catalog import BASES, job_name

GAMES = [
    "logos",
    "z80",
    "1942",
    "alibaba",
    "amidar",
    "anteater",
    "asteroids",
    "bagman",
    "bnj",
    "bombjack",
    "centipede",
    "millipede",
    "btime",
    "circusc",
    "commando",
    "crush",
    "digdug",
    "digdug2",
    "dkong",
    "dkong3",
    "dkongjr",
    "eyes",
    "frogger",
    "galaga",
    "galaxian",
    "gaplus",
    "gyruss",
    "invaders",
    "ladybug",
    "lizwiz",
    "llander",
    "m6502",
    "mappy",
    "mario",
    "mooncresta",
    "motorace",
    "mrdo",
    "mrtnt",
    "mspacman",
    "nibbler",
    "pacman",
    "pbaction",
    "pengo",
    "phoenix",
    "pooyan",
    "roadfighter",
    "rocnrope",
    "scramble",
    "scregg",
    "starforce",
    "baluba",
    "supercobra",
    "theglob",
    "timeplt",
    "todruaga",
    "tp84",
    "turtles",
    "tutankhm",
    "vanvan",
    "vanguard",
    "fantasy",
    "xevious",
    "zaxxon",
    "thepit",
    "bublbobl",
    "atetris",
    "ccastles",
    "bzone",
    "tempest",
    "spacduel",
    "gravitar",
    "bwidow",
    "berzerk",
    "starwars",
    "missile",
    "mpatrol",
    "starjack",
    "regulus",
    "upndown",
    "raflesia",
    "swat",
    "cbdash",
    "phozon",
    "gberet",
    "hero",
    "gng",
    "marble",
    "defender",
    "robotron",
    "joust",
    "sinistar",
    "1943",
    "gorf",
    "solomon",
    "flicky",
    "pitfall2",
    "mrviking",
    "imsorry",
    "wboy",
    "fourdwarrio",
    "hvymetal",
    "sxevious",
    "mikie",
    "vulgus",
    "cavenger",
    "gardia",
    "pleiads",
    "arkanoid",
    "yiear",
    "trackfld",
    "tron",
    "dotron",
    "tapper",
    "shollow",
    "motos",
    "jungleh",
    "megazone",
    "elevator",
    "mhavoc",
    "choplift",
    "tokisens",
]

def with_bases(names):
    """Each game after the games whose assets its bundle also holds."""
    out = []
    for name in names:
        for base in BASES.get(name, ()):
            if job_name(base) not in out and job_name(base) not in names:
                out.append(job_name(base))
        out.append(name)
    return out


games = GAMES if len(sys.argv[1:]) == 0 else with_bases(sys.argv[1:])
info(f"--------- Convert {'all' if games is GAMES else ' '.join(games)} ---------")
run("internal/pokey_poly.py")
run("internal/pyconv/make_unpack_fixture.py")

for game in games:
    run(f"internal/pyconv/conv_{game}.py")

# mode 3 (ASSETS_REMOTE): gating macros, then the bundles of the converted catalog games
info("--------- Catalog and remote bundles ---------")
try:
    code = runner.run(["catalog", "bundles"], jobs=1, requested=(*games, *(j.name for j in registry.ALL)))
except ConversionError as e:
    fatal(str(e))
if code != 0:
    fatal("catalog or bundles failed")

info("--- Success ---")
