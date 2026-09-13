#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="turtles")

info("--------- Convert Turtles ---------")
info("Converting Turtles")
run_in_workdir("turtles_rom_convert.py")

info("--- Success ---")
