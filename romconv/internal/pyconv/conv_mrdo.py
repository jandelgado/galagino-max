#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/mrdo")

info("--------- Convert MrDo ---------")
info("MrDo Unpack roms")
run("internal/unpack.py", "mrdo.zip")

info("MrDo CPU code")
run_in_workdir("cpu_conv.py")

info("MrDo Tiles")
run_in_workdir("bg_tiles.py")
run_in_workdir("fg_tiles.py")

info("MrDo Sprites")
run_in_workdir("Sprites.py")

info("MrDo Colormaps")
run_in_workdir("Palette_mrdo.py")
run_in_workdir("sprite_colormap.py")

info("--- Success ---")
