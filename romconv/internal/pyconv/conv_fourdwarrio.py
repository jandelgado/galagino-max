#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/system1")
os.environ["ROMCONV_PLAN"] = "fourdwarrio"

info("--------- Convert 4-D Warriors ---------")
run_in_workdir("system1_rom_convert.py", "fourdwarrio")

run("internal/pyconv/arena_plan.py", "fourdwarrio")
info("--- Success ---")
