#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/gravitar")

info("--------- Convert Gravitar ---------")
info("Converting Gravitar ROMs")
run_in_workdir("gravitar_rom_convert.py")

info("--- Success ---")
