#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/gberet")

info("--------- Convert Green Beret ---------")
run_in_workdir("gberet_rom_convert.py")

info("--- Success ---")
