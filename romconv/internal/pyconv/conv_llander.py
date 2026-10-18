#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/llander")

info("--------- Convert Lunar Lander ---------")
info("Converting Lunar Lander ROMs")
run_in_workdir("llander_rom_convert.py")

info("--- Success ---")
