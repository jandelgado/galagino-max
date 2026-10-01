#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="amidar")

info("--------- Convert Amidar ---------")
info("Converting Amidar")
run_in_workdir("amidar_rom_convert.py")

info("--- Success ---")
