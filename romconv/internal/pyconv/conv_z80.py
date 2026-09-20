#!/usr/bin/env python3
from convutil import run, info

info("--------- Convert Z80 ---------")
info("Z80")
run("internal/z80patch.py")

info("--- Success ---")
