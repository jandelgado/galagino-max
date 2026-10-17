#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/williams")
os.environ["ROMCONV_PLAN"] = "defender"

info("--------- Convert Defender ---------")
run_in_workdir("williams_rom_convert.py", "defender")
run("internal/pyconv/arena_plan.py", "defender")
info("--- Success ---")
