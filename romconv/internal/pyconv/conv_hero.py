#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/hero")

info("--------- Convert H.E.R.O. ---------")
run_in_workdir("hero_rom_convert.py")

info("--- Success ---")
