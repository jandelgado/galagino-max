#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/circusc")

info("--------- Convert Circus Charlie ---------")
info("Converting Circus Charlie (tiles+sprites+palette+roms)")
run_in_workdir("circusc_rom_convert.py")

info("--- Success ---")
