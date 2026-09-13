#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="bombjack")

info("--------- Convert Bombjack ---------")
info("Bombjack Unpack roms")
run("unpack.py", "bombjack.zip")

info("Bombjack CPU code")
run_in_workdir("cpu_conv.py")
run_in_workdir("audio_cpu_conv.py")

info("Bombjack Tiles")
run_in_workdir("tiles_bg_conv.py")
run_in_workdir("tiles_fg_conv.py")
run_in_workdir("bgmaps_conv.py")

info("Bombjack Sprites")
run_in_workdir("sprites_conv.py")

info("--- Success ---")
