#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="pengo")

info("--------- Convert Pengo ---------")
info("Pengo Unpack roms")
run("unpack.py", "pengo2u.zip")

info("Pengo CPU code")
run_in_workdir("cpu_conv.py")
run_in_workdir("audio_conv.py")

info("Peno Tiles")
run_in_workdir("tiles_fg_conv.py")

info("Peno Sprites")
run_in_workdir("sprites_conv.py")

info("Pengo Colormaps")
run_in_workdir("colormap.py")

info("--- Success ---")
