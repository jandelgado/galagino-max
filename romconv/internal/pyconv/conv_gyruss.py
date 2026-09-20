#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/gyruss")

info("--------- Convert Gyruss ---------")
info("Gyruss Unpack roms")
run("internal/unpack.py", "gyruss.zip")

info("Converting Gyruss")
run_in_workdir("gyruss_rom_convert.py")

info("--- Success ---")
