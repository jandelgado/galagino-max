#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/mikie")

info("--------- Convert Mikie ---------")
run_in_workdir("mikie_rom_convert.py")
info("--- Success ---")
