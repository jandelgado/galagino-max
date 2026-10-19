#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/xevious")

info("--------- Convert Super Xevious ---------")
run_in_workdir("sxevious_rom_convert.py")

info("--- Success ---")
