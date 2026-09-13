#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="dkong3")

info("--------- Convert Donkey Kong 3 ---------")
info("Donkey Kong Junior Unpack roms")
run("unpack.py", "dkong3.zip")

info("Donkey Kong 3 CPU code")
run_in_workdir("cpu_conv.py")
run_in_workdir("sound_conv.py")

info("Donkey Kong 3 Tiles")
run_in_workdir("tilemap_conv.py")

info("Donkey Kong 3 Colormaps")
run_in_workdir("cmap_conv.py")
run_in_workdir("color_codes_conv.py")

info("Donkey Kong 3 Sprites")
run_in_workdir("sprites_conv.py")

info("--- Success ---")
