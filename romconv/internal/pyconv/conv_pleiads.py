#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/pleiads")

info("--------- Convert Pleiads ---------")
run_in_workdir("pleiads_rom_convert.py")
info("--- Success ---")
