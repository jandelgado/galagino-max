#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="mspacman")

info("--------- Convert MsPacman ---------")
info("MsPacman Unpack roms")
run("unpack.py", "mspacman.zip")

info("Converting MsPacman")
run_in_workdir("mspacman_rom_convert.py")

info("--- Success ---")
