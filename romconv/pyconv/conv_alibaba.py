#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="alibaba")

info("--------- Convert Alibaba ---------")
run_in_workdir("alibaba_rom_convert.py")

info("--- Success ---")
