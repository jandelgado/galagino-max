#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/spacduel")

info("--------- Convert Space Duel ---------")
info("Converting Space Duel ROMs")
run_in_workdir("spacduel_rom_convert.py")

info("--- Success ---")
