#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/digdug2")

info("--------- Convert Dig Dug II ---------")
info("Dig Dug II Unpack roms")
run("internal/unpack.py", "digdug2.zip")

info("Converting Dig Dug II (tiles+sprites+palette+roms+wavetable)")
run_in_workdir("digdug2_rom_convert.py")

info("--- Success ---")
