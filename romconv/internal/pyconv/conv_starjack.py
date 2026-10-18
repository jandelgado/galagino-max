#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/system1")

info("--------- Convert Star Jacker ---------")
run_in_workdir("system1_rom_convert.py", "starjack")

info("--- Success ---")
