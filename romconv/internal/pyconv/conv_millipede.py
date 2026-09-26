#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/millipede")

info("--------- Convert Millipede ---------")
info("Converting Millipede ROMs")
run_in_workdir("millipede_rom_convert.py")

info("--- Success ---")
