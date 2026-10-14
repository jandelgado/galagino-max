#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/williams")

info("--------- Convert Joust ---------")
run_in_workdir("williams_rom_convert.py", "joust")
info("--- Success ---")
