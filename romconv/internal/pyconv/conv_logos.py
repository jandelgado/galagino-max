#!/usr/bin/env python3
from pathlib import Path

from convutil import info, run

# PNG names that differ from the machine dir (kept for README links)
MACHINE = {"invaders": "spaceinvaders", "milliped": "millipede"}

info("--------- Convert menu logos ---------")
for png in sorted(Path("../logos").glob("*.png")):
    name = MACHINE.get(png.stem, png.stem)
    machine = Path("../source/src/machines") / name
    if machine.is_dir():
        run("internal/logoconv.py", str(png), str(machine / f"{name}_logo.h"))
