#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="pooyan")

info("--------- Convert Pooyan ---------")
info("Pooyan Unpack roms")
run("unpack.py", "pooyan.zip")

info("Converting Pooyan")
run_in_workdir("pooyan_rom_convert.py")

info("--- Success ---")
