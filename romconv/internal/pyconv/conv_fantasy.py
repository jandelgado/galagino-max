#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/fantasy")

info("--------- Convert Fantasy ---------")
run_in_workdir("fantasy_rom_convert.py")

info("--- Success ---")
