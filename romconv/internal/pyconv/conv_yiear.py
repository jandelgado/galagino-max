#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/yiear")

info("--------- Convert Yie Ar Kung-Fu ---------")
run_in_workdir("yiear_rom_convert.py")
info("--- Success ---")
