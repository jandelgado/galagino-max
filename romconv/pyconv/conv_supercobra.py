#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="supercobra")

info("--------- Convert Super Cobra ---------")
info("Converting Super Cobra")
run_in_workdir("supercobra_rom_convert.py")

info("--- Success ---")
