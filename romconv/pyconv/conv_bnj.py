#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="bnj")

info("--------- Convert Bump'n'Jump ---------")
info("Converting Bump'n'Jump (tiles+sprites+rom)")
run_in_workdir("bnj_rom_convert.py")

info("--- Success ---")
