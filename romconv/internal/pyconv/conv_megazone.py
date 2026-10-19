#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/megazone")

info("--------- Convert Mega Zone ---------")
run_in_workdir("megazone_rom_convert.py")
info("--- Success ---")
