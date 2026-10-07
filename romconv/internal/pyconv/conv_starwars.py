#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/starwars")

info("--------- Convert Star Wars ---------")
info("Converting Star Wars ROMs")
run_in_workdir("starwars_rom_convert.py")

info("--- Success ---")
