#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/centipede")

info("--------- Convert Centipede ---------")
info("Converting Centipede ROMs")
run_in_workdir("centipede_rom_convert.py")

info("--- Success ---")
