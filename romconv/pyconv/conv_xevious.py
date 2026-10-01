#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="xevious")

info("--------- Convert Xevious ---------")
info("Converting Xevious (tiles+sprites+palette+roms+planetmap+wavetable)")
run_in_workdir("xevious_rom_convert.py")

info("--- Success ---")
