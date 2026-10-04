#!/usr/bin/env -S uv run
import sys

from internal.pyconv.convutil import info, run

GAMES = [
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
    "crush",
    "digdug",
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
    "m6502",
    "mappy",
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
    "supercobra",
    "theglob",
    "timeplt",
    "todruaga",
    "turtles",
    "tutankhm",
    "vanvan",
    "vanguard",
    "fantasy",
    "xevious",
    "zaxxon",
]

games = GAMES if len(sys.argv[1:]) == 0 else sys.argv[1:]
info(f"--------- Convert {'all' if games is GAMES else ' '.join(games)} ---------")

for game in games:
    run(f"internal/pyconv/conv_{game}.py")

info("--- Success ---")
