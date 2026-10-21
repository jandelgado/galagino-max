#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/elevator")

info("--------- Convert Elevator Action ---------")
run_in_workdir("elevator_rom_convert.py")
info("--- Success ---")
