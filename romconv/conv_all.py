#!/usr/bin/env python3
import sys

from pyconv.convutil import info, run

info("--------- Convert all ---------")

GAMES = [
    "z80",
    "1942",
    "alibaba",
    "amidar",
    "anteater",
    "bagman",
    "bnj",
    "bombjack",
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
]

games = GAMES if len(sys.argv[1:]) == 0 else sys.argv[1:]
for game in games:
    run(f"pyconv/conv_{game}.py")

info("--- Success ---")
