#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/system1")
os.environ["ROMCONV_PLAN"] = "pitfall2"

info("--------- Convert Pitfall II ---------")
run_in_workdir("system1_rom_convert.py", "pitfall2")

run("internal/pyconv/arena_plan.py", "pitfall2")
info("--- Success ---")
