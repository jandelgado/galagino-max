#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

os.environ["ROMCONV_PLAN"] = "gng"

run_in_workdir = partial(run, cwd="internal/gng")

info("--------- Convert Ghosts'n Goblins ---------")
info("GnG CPU ROMs, chars, bg tiles (deduplicated), sprites")
run_in_workdir("gng_rom_convert.py")

info("GnG C64 SID music")
run_in_workdir("gng_sid_convert.py")

run("internal/pyconv/arena_plan.py", "gng")
info("--- Success ---")
