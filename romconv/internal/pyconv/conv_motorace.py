#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/motorace")

info("--------- Convert Motorace USA ---------")
info("Converting Motorace USA")
run_in_workdir("motorace_rom_convert.py")

info("--- Success ---")
