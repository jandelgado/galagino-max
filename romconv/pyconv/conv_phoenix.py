#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="phoenix")

info("--------- Convert Phoenix ---------")
info("Converting Phoenix")
run_in_workdir("phoenix_rom_convert.py")

info("--- Success ---")
