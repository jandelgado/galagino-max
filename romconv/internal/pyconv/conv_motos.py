#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/digdug2")

info("--------- Convert Motos ---------")
info("Converting Motos (tiles+sprites+palette+roms+wavetable)")
run_in_workdir("digdug2_rom_convert.py", "motos")

info("--- Success ---")
