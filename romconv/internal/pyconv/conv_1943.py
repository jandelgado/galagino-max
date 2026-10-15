#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/1943")

info("--------- Convert 1943 (bootleg) ---------")
run_in_workdir("1943_rom_convert.py")

info("--- Success ---")
