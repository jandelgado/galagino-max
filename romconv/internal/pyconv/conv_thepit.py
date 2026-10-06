#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/thepit")

info("--------- Convert The Pit ---------")
info("Converting The Pit ROMs")
run_in_workdir("thepit_rom_convert.py")

info("--- Success ---")
