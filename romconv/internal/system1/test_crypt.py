#!/usr/bin/env python3
"""Check romconv's System 1 main CPU decryption against MAME dumps.

Usage: test_crypt.py DUMP_DIR GAME...
DUMP_DIR/<mame set>/{data,ops}.bin come from mame_opcodes.lua (main CPU
program and opcodes spaces, 0000-bfff, read after frame 1).
"""
import os
import sys

import system1_rom_convert as conv

CRYPT_END = 0x8000  # opcode image: 0000-7fff
DATA_END = 0xc000   # 8000-bfff: fixed ROM or bank 0


def check(dumps, game):
    spec = conv.GAMES[game]
    zip_path = os.path.join(conv.ROOT, "romszip", spec.get("zip", f"{game}.zip"))
    data, ops = conv.decrypt_main(spec, conv.load(zip_path, spec["main"]))
    set_dir = os.path.join(dumps, spec.get("mame_set", game))
    want_data = open(os.path.join(set_dir, "data.bin"), "rb").read()
    want_ops = open(os.path.join(set_dir, "ops.bin"), "rb").read()
    errors = []
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
