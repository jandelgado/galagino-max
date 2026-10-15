#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/bwidow")

info("--------- Convert Black Widow ---------")
info("Converting Black Widow ROMs")
run_in_workdir("bwidow_rom_convert.py")

info("--- Success ---")
