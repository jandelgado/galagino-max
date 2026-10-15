#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/gorf")

info("--------- Convert Gorf ---------")
run_in_workdir("gorf_rom_convert.py")
info("--- Success ---")
