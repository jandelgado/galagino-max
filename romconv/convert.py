#!/usr/bin/env -S uv run
import sys

from internal.pyconv.convutil import fatal, info, run
from romconv import registry
from romconv.core import runner
from romconv.core.errors import ConversionError
from romconv.remote.catalog import BASES, job_name

GAMES = [
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


names = sys.argv[1:]
registry_names = {j.name for j in registry.ALL}
ordered = with_bases(names)
legacy = GAMES if not names else [n for n in ordered if n not in registry_names]
ported = [] if not names else [n for n in ordered if n in registry_names]
info(f"--------- Convert {'all' if not names else ' '.join(ordered)} ---------")

for game in legacy:
    run(f"internal/pyconv/conv_{game}.py")

# ported jobs, then mode 3 (ASSETS_REMOTE): gating macros and the bundles of the converted catalog games
info("--------- Ported jobs, catalog and remote bundles ---------")
requested = (*legacy, *registry_names) if not names else tuple(ordered)
try:
    code = runner.run(ported or (["bundles"] if names else []), jobs=1, requested=requested)
except ConversionError as e:
    fatal(str(e))
if code != 0:
    fatal("a ported job failed")

info("--- Success ---")
