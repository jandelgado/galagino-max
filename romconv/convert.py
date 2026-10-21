#!/usr/bin/env -S uv run
import os
import sys

from internal.pyconv.convutil import fatal, info, run
from romconv import registry
from romconv.core import runner
from romconv.core.errors import ConversionError
from romconv.remote.catalog import BASES, job_name

GAMES = [
    "alibaba",
    "asteroids",
    "bnj",
    "bombjack",
    "centipede",
    "millipede",
    "btime",
    "circusc",
    "commando",
    "dkong3",
    "dkongjr",
    "gyruss",
    "invaders",
    "ladybug",
    "llander",
    "mario",
    "motorace",
    "mrdo",
    "mspacman",
    "nibbler",
    "pbaction",
    "pengo",
    "phoenix",
    "pooyan",
    "roadfighter",
    "rocnrope",
    "scregg",
    "starforce",
    "baluba",
    "timeplt",
    "tp84",
    "tutankhm",
    "vanguard",
    "fantasy",
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
    "jungleh",
    "megazone",
    "elevator",
    "mhavoc",
    "choplift",
    "tokisens",
]


def with_bases(names):
    """Each still-legacy game after the games whose assets its bundle also
    holds. Ported jobs name them as Job.deps instead."""
    out = []
    for name in names:
        for base in BASES.get(name, ()) if name in GAMES else ():
            if job_name(base) not in out and job_name(base) not in names:
                out.append(job_name(base))
        out.append(name)
    return out


def parse_args(args):
    """([-j N | -jN] [name ...]) -> (jobs, names)"""
    jobs, names = os.cpu_count() or 1, []
    it = iter(args)
    for arg in it:
        if arg == "-j":
            arg = "-j" + next(it, "")
        if arg.startswith("-j") and arg[2:].isdigit() and int(arg[2:]) > 0:
            jobs = int(arg[2:])
        elif arg.startswith("-"):
            fatal(f"usage: convert.py [-j N] [name ...]  (bad option {arg!r})")
        else:
            names.append(arg)
    return jobs, names


jobs, names = parse_args(sys.argv[1:])
registry_names = [j.name for j in registry.ALL]
unknown = [n for n in names if n not in GAMES and n not in registry_names]
if unknown:
    fatal(f"unknown name {' '.join(unknown)}; see GAMES in convert.py and romconv/registry.py")

# legacy drivers run first, one after the other; ported jobs run in the pool
ordered = with_bases(names)
legacy = GAMES if not names else [n for n in ordered if n in GAMES]
ported = [n for n in ordered if n in registry_names]
info(f"--------- Convert {'all' if not names else ' '.join(names)} ---------")

for game in legacy:
    run(f"internal/pyconv/conv_{game}.py")

# Ported jobs, then mode 3 (ASSETS_REMOTE): gating macros and the bundles of the
# requested catalog games. A partial run asks for what the user named: bases
# run to feed the sidecars, but their bundles are not rebuilt.
info("--------- Ported jobs, catalog and remote bundles ---------")
requested = (*GAMES, *registry_names) if not names else tuple(names)
try:
    code = runner.run(ported or (["bundles"] if names else []), jobs=jobs, requested=requested)
except ConversionError as e:
    fatal(str(e))
if code != 0:
    fatal("a ported job failed")

info("--- Success ---")
