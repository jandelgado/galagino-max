#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/mario")

info("--------- Convert Mario Bros. ---------")
run_in_workdir("mario_rom_convert.py")
info("--- Success ---")
