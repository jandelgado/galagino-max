#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="zaxxon")

info("--------- Convert Zaxxon ---------")
info("Converting Zaxxon (tiles+sprites+tilemap+rom+samples)")
run_in_workdir("zaxxon_rom_convert.py")

info("--- Success ---")
