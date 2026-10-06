#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/ccastles")

info("--------- Convert Crystal Castles ---------")
info("Converting Crystal Castles ROMs")
run_in_workdir("ccastles_rom_convert.py")

info("--- Success ---")
