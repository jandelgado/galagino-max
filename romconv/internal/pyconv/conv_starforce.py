#!/usr/bin/env python3
import os
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/starforce")
os.environ["ROMCONV_PLAN"] = "starforce"

info("--------- Convert Star Force ---------")
info("Star Force Unpack roms")
run("internal/unpack.py", "starforc.zip")

info("Star Force CPU code")
run_in_workdir("cpu_conv.py", "starforce")

info("Star Force Tiles")
run_in_workdir("bg1_tiles.py", "starforce")
run_in_workdir("bg2_tiles.py", "starforce")
run_in_workdir("bg3_tiles.py", "starforce")
run_in_workdir("fg_tiles.py", "starforce")

info("Star Force Sprites")
run_in_workdir("sprites.py", "starforce")

info("Star Force Palette RBG565")
run_in_workdir("starforce_palette.py")

run("internal/pyconv/arena_plan.py", "starforce")
info("--- Success ---")
