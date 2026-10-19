#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/dotron")

info("--------- Convert Discs of Tron ---------")
run_in_workdir("dotron_rom_convert.py")
info("--- Success ---")
