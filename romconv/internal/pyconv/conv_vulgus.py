#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/vulgus")

info("--------- Convert Vulgus ---------")
run_in_workdir("vulgus_rom_convert.py")
info("--- Success ---")
