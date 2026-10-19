#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/system1")
os.environ["ROMCONV_PLAN"] = "mrviking"

info("--------- Convert Mister Viking ---------")
run_in_workdir("system1_rom_convert.py", "mrviking")

run("internal/pyconv/arena_plan.py", "mrviking")
info("--- Success ---")
