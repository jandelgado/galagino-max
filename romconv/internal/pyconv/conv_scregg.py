#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/scregg")

info("--------- Convert Scrambled Egg ---------")
info("Converting Scrambled Egg")
run_in_workdir("scregg_rom_convert.py")

info("--- Success ---")
