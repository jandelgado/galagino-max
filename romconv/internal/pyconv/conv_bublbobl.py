#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/bublbobl")

info("--------- Convert Bubble Bobble (boblbobl) ---------")
run_in_workdir("bublbobl_rom_convert.py")
info("--- Success ---")
