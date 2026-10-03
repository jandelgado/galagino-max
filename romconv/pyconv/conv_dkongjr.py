#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="dkongjr")

info("--------- Convert Donkey Kong Junior ---------")
info("Donkey Kong Junior Unpack roms")
run("unpack.py", "dkongjrj.zip")

info("Donkey Kong Junior CPU code")
run_in_workdir("cpu_conv.py")
run_in_workdir("sound_conv.py")

info("Donkey Kong Junior Tiles")
run_in_workdir("tilemap_conv.py")

info("Donkey Kong Junior Colormaps")
run_in_workdir("cmap_conv.py")

info("Donkey Kong Junior Sprites")
run_in_workdir("sprites_conv.py")

info("--- Success ---")
