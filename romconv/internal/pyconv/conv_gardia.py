#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/system1")
os.environ["ROMCONV_PLAN"] = "gardia"

info("--------- Convert Gardia ---------")
run_in_workdir("system1_rom_convert.py", "gardia")

run("internal/pyconv/arena_plan.py", "gardia")
info("--- Success ---")
