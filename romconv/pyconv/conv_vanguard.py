#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="vanguard")

info("--------- Convert Vanguard ---------")
info("Converting Vanguard ROMs")
run_in_workdir("vanguard_rom_convert.py")

info("--- Success ---")
