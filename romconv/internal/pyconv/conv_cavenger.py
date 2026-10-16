#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/cavenger")

info("--------- Convert Cosmic Avenger ---------")
run_in_workdir("cavenger_rom_convert.py")
info("--- Success ---")
