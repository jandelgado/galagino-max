#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

os.environ["ROMCONV_PLAN"] = "bublbobl"

run_in_workdir = partial(run, cwd="internal/bublbobl")

info("--------- Convert Bubble Bobble (boblbobl) ---------")
run_in_workdir("bublbobl_rom_convert.py")
run("internal/pyconv/arena_plan.py", "bublbobl")
info("--- Success ---")
