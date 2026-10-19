#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

os.environ["ROMCONV_PLAN"] = "bnj"

run_in_workdir = partial(run, cwd="internal/bnj")

info("--------- Convert Bump'n'Jump ---------")
info("Converting Bump'n'Jump (tiles+sprites+rom)")
run_in_workdir("bnj_rom_convert.py")

run("internal/pyconv/arena_plan.py", "bnj")
info("--- Success ---")
