#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/cbdash")

info("--------- Convert Boulder Dash ---------")
run_in_workdir("cbdash_rom_convert.py")

info("--- Success ---")
