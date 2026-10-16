#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/arkanoid")

info("--------- Convert Arkanoid ---------")
run_in_workdir("arkanoid_rom_convert.py")
info("--- Success ---")
