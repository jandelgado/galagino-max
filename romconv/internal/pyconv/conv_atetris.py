#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/atetris")

info("--------- Convert Tetris ---------")
info("Converting Tetris ROMs")
run_in_workdir("atetris_rom_convert.py")

info("--- Success ---")
