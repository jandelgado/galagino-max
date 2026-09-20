#!/usr/bin/env python3
from convutil import run, info

info("--------- Convert M6502 ---------")
info("M6502")
run("internal/m6502patch.py")

info("--- Success ---")
