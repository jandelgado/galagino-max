#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="galaxian")

info("--------- Convert Galaxian ---------")
info("Galaxian Unpack roms")
run("unpack.py", "galaxian.zip")

info("Converting Galaxian")
run_in_workdir("galaxian_rom_convert.py")

info("--- Success ---")
