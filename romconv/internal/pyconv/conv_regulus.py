#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/system1")
os.environ["ROMCONV_PLAN"] = "regulus"

info("--------- Convert Regulus ---------")
run_in_workdir("system1_rom_convert.py", "regulus")

run("internal/pyconv/arena_plan.py", "regulus")
info("--- Success ---")
