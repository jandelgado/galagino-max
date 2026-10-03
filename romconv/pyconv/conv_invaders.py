#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="invaders")

info("--------- Convert Space Invaders ---------")
info("Space Invaders Unpack roms")
run("unpack.py", "invaders.zip")

info("Converting Space Invaders")
run_in_workdir("invaders_rom_convert.py")
run_in_workdir("samples_convert.py")

info("--- Success ---")
