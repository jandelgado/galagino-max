#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="roadfighter")

info("--------- Convert Road Fighter ---------")
info("Converting Road Fighter")
run_in_workdir("roadfighter_rom_convert.py")

info("--- Success ---")
