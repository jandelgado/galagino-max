#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/tapper")

info("--------- Convert Tapper ---------")
run_in_workdir("tapper_rom_convert.py")
info("--- Success ---")
