#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/solomon")

info("--------- Convert Solomon's Key ---------")
run_in_workdir("solomon_rom_convert.py")

info("--- Success ---")
