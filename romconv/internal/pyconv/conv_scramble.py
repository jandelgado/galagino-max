#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/scramble")

info("--------- Convert Scramble ---------")
info("Converting Scramble")
run_in_workdir("scramble_rom_convert.py")

info("--- Success ---")
