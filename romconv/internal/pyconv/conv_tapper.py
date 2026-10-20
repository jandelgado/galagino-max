#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/tapper")
os.environ["ROMCONV_PLAN"] = "tapper"

info("--------- Convert Tapper ---------")
run_in_workdir("tapper_rom_convert.py")

run("internal/pyconv/arena_plan.py", "tapper")
info("--- Success ---")
