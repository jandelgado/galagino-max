#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/tutankhm")

info("--------- Convert Tutankham ---------")
info("Tutankham Unpack roms")
run("internal/unpack.py", "tutankhm.zip")

info("Converting Tutankham")
run_in_workdir("tutankhm_rom_convert.py")

info("--- Success ---")
