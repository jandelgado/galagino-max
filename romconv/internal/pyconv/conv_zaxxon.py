#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

os.environ["ROMCONV_PLAN"] = "zaxxon"

run_in_workdir = partial(run, cwd="internal/zaxxon")

info("--------- Convert Zaxxon ---------")
info("Converting Zaxxon (tiles+sprites+tilemap+rom+samples)")
run_in_workdir("zaxxon_rom_convert.py")

run("internal/pyconv/arena_plan.py", "zaxxon")
info("--- Success ---")
