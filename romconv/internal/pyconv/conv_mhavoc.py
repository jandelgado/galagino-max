#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/mhavoc")

info("--------- Convert Major Havoc ---------")
run_in_workdir("mhavoc_rom_convert.py")
info("--- Success ---")
