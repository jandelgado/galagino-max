#!/usr/bin/env python3
from functools import partial
from convutil import run, info

# Baluba runs on the Star Force board: reuse its converters with the baluba ROM set
run_in_workdir = partial(run, cwd="internal/starforce")

info("--------- Convert Baluba-louk no Densetsu ---------")
info("Baluba Unpack roms")
run("internal/unpack.py", "baluba.zip")

for script in ("cpu_conv.py", "bg1_tiles.py", "bg2_tiles.py", "bg3_tiles.py",
               "fg_tiles.py", "sprites.py"):
    run_in_workdir(script, "baluba")

info("--- Success ---")
