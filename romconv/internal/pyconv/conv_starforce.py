#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/starforce")

info("--------- Convert Star Force ---------")
info("Star Force Unpack roms")
run("internal/unpack.py", "starforc.zip")

info("Star Force CPU code")
run_in_workdir("cpu_conv.py")

info("Star Force Tiles")
run_in_workdir("bg1_tiles.py")
run_in_workdir("bg2_tiles.py")
run_in_workdir("bg3_tiles.py")
run_in_workdir("fg_tiles.py")

info("Star Force Sprites")
run_in_workdir("sprites.py")

info("Star Force Palette RBG565")
run_in_workdir("starforce_palette.py")

info("--- Success ---")
