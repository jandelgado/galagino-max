#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="gaplus")

info("--------- Convert Gaplus ---------")
info("Gaplus Logo")

info("Converting Gaplus (tiles+sprites+rom)")
run_in_workdir("gaplus_rom_convert.py")

info("--- Success ---")
