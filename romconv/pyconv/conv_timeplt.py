#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="timeplt")

info("--------- Convert Time Pilot ---------")
info("Time Pilot Unpack roms")
run("unpack.py", "timeplt.zip")

info("Converting Time Pilot")
run_in_workdir("timeplt_rom_convert.py")

info("--- Success ---")
