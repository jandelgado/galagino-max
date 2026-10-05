#!/usr/bin/env python3
from functools import partial
from convutil import run, info

run_in_workdir = partial(run, cwd="internal/commando")

info("--------- Convert Commando (C64 SID test drive) ---------")
run_in_workdir("commando_sid_convert.py")

info("--- Success ---")
