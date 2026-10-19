#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

os.environ["ROMCONV_PLAN"] = "marble"

run_in_workdir = partial(run, cwd="internal/marble")

info("--------- Convert Marble Madness ---------")
run_in_workdir("marble_rom_convert.py")

run("internal/pyconv/arena_plan.py", "marble")
info("--- Success ---")
