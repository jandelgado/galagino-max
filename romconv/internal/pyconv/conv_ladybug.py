#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/ladybug")

info("--------- Convert Ladybug ---------")
info("Ladybug Unpack roms")
run("internal/unpack.py", "ladybug.zip")

info("Converting Ladybug")
run_in_workdir("ladybug_rom_convert.py")

info("--- Success ---")
