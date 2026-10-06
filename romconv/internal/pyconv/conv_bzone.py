#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/bzone")

info("--------- Convert Battle Zone ---------")
info("Converting Battle Zone ROMs")
run_in_workdir("bzone_rom_convert.py")

info("--- Success ---")
