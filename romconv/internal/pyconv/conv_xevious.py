#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/xevious")

info("--------- Convert Xevious ---------")
info("Converting Xevious (tiles+sprites+palette+roms+planetmap+wavetable)")
run_in_workdir("xevious_rom_convert.py")

info("Namco 54xx ROM (ships in galaga.zip, shared with Galaga)")
run("internal/unpack.py", "galaga.zip")
run("internal/romconv.py", "namco54_rom", "./roms/54xx.bin", "../source/src/machines/galaga/namco54_rom.h")

info("--- Success ---")
