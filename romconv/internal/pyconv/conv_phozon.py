#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/phozon")

info("--------- Convert Phozon ---------")
run_in_workdir("phozon_rom_convert.py")

info("--- Success ---")
