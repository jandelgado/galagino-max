#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/tempest")

info("--------- Convert Tempest ---------")
info("Converting Tempest ROMs")
run_in_workdir("tempest_rom_convert.py")

info("--- Success ---")
