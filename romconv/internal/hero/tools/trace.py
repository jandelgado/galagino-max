#!/usr/bin/env python3
"""Per-frame RAM trace of H.E.R.O. in MAME for scripted input.

usage: trace.py <rom.a26> <script.txt> <frames> <out.csv> [vars]
script: harness.lua format ("<frame> <keys>", "P <frame> <addr> <val>").
vars: comma separated RAM addresses (hex), default the hero movement set;
100 = last SWCHA read, 101 = last INPT4 read.
CSV: frame (game frame, counted at its $81 increment, see harness.lua
H_RAM_TAP), then one hex column per variable at that point of the loop.
"""
import os
import subprocess
import sys
import tempfile

REC = 130  # harness.lua H_RAM_TAP record: RAM $80-$FF, SWCHA, INPT4
DEFAULT = "81,84,9b,9c,9f,e9,b0,ac,ad,e6,fc,e7,e8,e2,e3,e4,f5,a0,b2,ab,af"

rom, script, frames, out = sys.argv[1:5]
names = (sys.argv[5] if len(sys.argv) > 5 else DEFAULT).split(",")
here = os.path.dirname(os.path.abspath(__file__))
with tempfile.TemporaryDirectory() as work:
    env = dict(os.environ, H_SCRIPT=os.path.abspath(script), H_FRAMES=frames, H_RAM_TAP="1")
    subprocess.run([os.path.join(here, "run_mame.sh"), rom, work], env=env, check=True,
                   stdout=subprocess.DEVNULL)
    ram = open(os.path.join(work, "ram.bin"), "rb").read()
with open(out, "w") as f:
    print("frame," + ",".join(names), file=f)
    for i in range(len(ram) // REC):
        row = ram[i * REC:(i + 1) * REC]
        print(f"{i + 1}," + ",".join(f"{row[int(n, 16) - 0x80]:02x}" for n in names), file=f)
print(f"{len(ram) // REC} frames -> {out}")
