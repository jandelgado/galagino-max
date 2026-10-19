#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/tron")

info("--------- Convert Tron ---------")
run_in_workdir("tron_rom_convert.py")
info("--- Success ---")
