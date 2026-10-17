#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/system1")
os.environ["ROMCONV_PLAN"] = "hvymetal"

info("--------- Convert Heavy Metal ---------")
run_in_workdir("system1_rom_convert.py", "hvymetal")

run("internal/pyconv/arena_plan.py", "hvymetal")
info("--- Success ---")
