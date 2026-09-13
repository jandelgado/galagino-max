#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="todruaga")

info("--------- Convert The Tower of Druaga ---------")
info("Tower of Druaga Unpack roms")
run("unpack.py", "todruaga.zip")

info("Converting Tower of Druaga (tiles+sprites+palette+roms+wavetable, con autotest)")
run_in_workdir("todruaga_rom_convert.py")

info("--- Success ---")
