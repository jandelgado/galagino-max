#!/usr/bin/env python3
"""Check romconv's System 1 main CPU decryption against MAME dumps.

Usage: test_crypt.py DUMP_DIR GAME...
DUMP_DIR/<mame set>/{data,ops}.bin come from mame_opcodes.lua (main CPU
program and opcodes spaces, 0000-bfff, read after frame 1). Banked MC-8123
sets compare the whole image: 0000-7fff plus every bank (mame_opcodes.lua
with BANKS=n).
"""
import logging
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.core.paths import default
from romconv.games.system1.crypt import decrypt_main
from romconv.games.system1.sets import SETS, load_roms

CRYPT_END = 0x8000  # opcode image: 0000-7fff
DATA_END = 0xc000   # 8000-bfff: fixed ROM or bank 0


def check(dumps, game):
    spec = SETS[game]
    zip = spec.zip or f"{game}.zip"
    ctx = Context(Job(game, lambda c: None, zips=(zip,)), default(), (), logging.Logger(game))
    data, ops = decrypt_main(ctx, spec, load_roms(ctx, spec.main, zip), zip)
    set_dir = os.path.join(dumps, spec.mame_set or game)
    want_data = open(os.path.join(set_dir, "data.bin"), "rb").read()
    want_ops = open(os.path.join(set_dir, "ops.bin"), "rb").read()
    errors = []
    if spec.crypt == "mc8123":
        if ops != want_ops:
            errors.append("opcodes")
        if data != want_data:
            errors.append("data")
        return errors
    if ops[:CRYPT_END] != want_ops[:CRYPT_END]:
        errors.append("opcodes 0000-7fff")
    if data[:DATA_END] != want_data[:DATA_END]:
        errors.append("data 0000-bfff")
    return errors


def main(dumps, games):
    failed = 0
    for game in games:
        errors = check(dumps, game)
        print(f"{game}: {'FAIL ' + ', '.join(errors) if errors else 'ok'}")
        failed += bool(errors)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
