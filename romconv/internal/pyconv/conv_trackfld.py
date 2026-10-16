#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/trackfld")

info("--------- Convert Track & Field ---------")
run_in_workdir("trackfld_rom_convert.py")
info("--- Success ---")
