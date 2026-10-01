#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="mooncresta")

info("--------- Convert Moon Cresta ---------")
info("Moon Cresta Unpack roms")
run("unpack.py", "mooncrst.zip")

info("Converting Moon Cresta")
run_in_workdir("mooncresta_rom_convert.py")

info("--- Success ---")
