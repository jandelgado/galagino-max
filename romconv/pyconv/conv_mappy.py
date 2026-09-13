#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="mappy")

info("--------- Convert Mappy ---------")
info("Converting Mappy (tiles+sprites+palette+roms+wavetable)")
run_in_workdir("mappy_rom_convert.py")

info("--- Success ---")
