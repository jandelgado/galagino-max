#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/asteroids")

info("--------- Convert Asteroids ---------")
info("Converting Asteroids ROMs")
run_in_workdir("asteroids_rom_convert.py")

info("--- Success ---")
