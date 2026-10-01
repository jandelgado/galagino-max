#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="nibbler")

info("--------- Convert Nibbler ---------")
run_in_workdir("nibbler_rom_convert.py")

info("--- Success ---")
