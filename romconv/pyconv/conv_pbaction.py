#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="pbaction")

info("--------- Convert Pinball Action ---------")
info("Converting Pinball Action")
run_in_workdir("pbaction_rom_convert.py")

info("--- Success ---")
