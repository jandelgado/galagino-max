#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/williams")
os.environ["ROMCONV_PLAN"] = "sinistar"

info("--------- Convert Sinistar ---------")
run_in_workdir("williams_rom_convert.py", "sinistar")
run("internal/pyconv/arena_plan.py", "sinistar")
info("--- Success ---")
