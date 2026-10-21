#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/elevator")

info("--------- Convert Jungle Hunt ---------")
run_in_workdir("elevator_rom_convert.py", "jungleh")
info("--- Success ---")
