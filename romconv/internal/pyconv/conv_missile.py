#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/missile")

info("--------- Convert Missile Command ---------")
info("Converting Missile Command ROMs")
run_in_workdir("missile_rom_convert.py")

info("--- Success ---")
