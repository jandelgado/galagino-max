#!/usr/bin/env python3
from functools import partial
from convutil import run

run_in_workdir = partial(run, cwd="rocnrope")

run_in_workdir("rocnrope_rom_convert.py")
