#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

os.environ["ROMCONV_PLAN"] = "scregg"

run_in_workdir = partial(run, cwd="internal/scregg")

info("--------- Convert Scrambled Egg ---------")
info("Converting Scrambled Egg")
run_in_workdir("scregg_rom_convert.py")

run("internal/pyconv/arena_plan.py", "scregg")
info("--- Success ---")
