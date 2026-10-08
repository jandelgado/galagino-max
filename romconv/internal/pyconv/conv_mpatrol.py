#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/mpatrol")

info("--------- Convert Moon Patrol ---------")
info("Converting Moon Patrol ROMs")
run_in_workdir("mpatrol_rom_convert.py")

info("--- Success ---")
