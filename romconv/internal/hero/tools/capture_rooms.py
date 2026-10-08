#!/usr/bin/env python3
"""Snapshot all 256 H.E.R.O. rooms in MAME (reference for roomcheck.py).

usage: capture_rooms.py <rom.a26> <outdir>
Starts game 1, then pokes level $F5, screen $9C and ball x $A0 (only reset
by a real screen change, see HERO.md) and snapshots 3 frames later.
"""
import os
import subprocess
import sys

rom_path, out = sys.argv[1], os.path.abspath(sys.argv[2])
rom = open(rom_path, "rb").read()
b1 = lambda a: rom[4096 + (a & 0xFFF)]
first = [b1(0xFF00 + l) for l in range(20)]
sizes = [b - a for a, b in zip(first, first[1:] + [256])]

os.makedirs(out, exist_ok=True)
lines = ["1 -", "100 S", "106 -"]
snaps = []
f = 200
for level in range(20):
    for screen in range(sizes[level]):
        fe = b1(0xFE00 + first[level] + screen) & 0xFC
        a0 = (fe | 1) if fe else 0
        if a0 not in (0x09, 0x99):
            a0 = 0x3D if screen == 0 else 0
        for k in (0, 1):
            lines += [f"P {f + k} f5 {level:x}", f"P {f + k} 9c {screen:x}", f"P {f + k} a0 {a0:x}"]
        snaps.append(f + 3)
        f += 4
script = os.path.join(out, "script.txt")
open(script, "w").write("\n".join(lines) + "\n")
env = dict(os.environ, H_SCRIPT=script, H_FRAMES=str(f), H_SNAP=",".join(map(str, snaps)))
here = os.path.dirname(os.path.abspath(__file__))
subprocess.run([os.path.join(here, "run_mame.sh"), rom_path, out], env=env, check=True)
# room order = frame order
for room, frame in enumerate(snaps):
    os.replace(os.path.join(out, f"snap_{frame:05d}.png"), os.path.join(out, f"room_{room:03d}.png"))
print(f"{len(snaps)} rooms in {out}")
