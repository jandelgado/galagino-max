#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="burgertime")

info("--------- Convert Burger Time ---------")
info("Burger Time Unpack roms")
run("unpack.py", "btime.zip")

info("Converting Burger Time (tiles+sprites+rom)")
run_in_workdir("burgertime_rom_convert.py")

info("--- Success ---")
