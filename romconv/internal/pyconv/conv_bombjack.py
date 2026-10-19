#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

os.environ["ROMCONV_PLAN"] = "bombjack"

run_in_workdir = partial(run, cwd="internal/bombjack")

info("--------- Convert Bombjack ---------")
info("Bombjack Unpack roms")
run("internal/unpack.py", "bombjack.zip")

info("Bombjack CPU code")
run_in_workdir("cpu_conv.py")
run_in_workdir("audio_cpu_conv.py")

info("Bombjack Tiles")
run_in_workdir("tiles_bg_conv.py")
run_in_workdir("tiles_fg_conv.py")
run_in_workdir("bgmaps_conv.py")

info("Bombjack Sprites")
run_in_workdir("sprites_conv.py")

run("internal/pyconv/arena_plan.py", "bombjack")
info("--- Success ---")
