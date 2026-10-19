#!/usr/bin/env python3
"""Every System 1 set's Arena assets fit in system1's ctor unpack order.

Usage: test_arena.py GAME...
"""
import sys

import system1_rom_convert as conv


def main(games):
    failed = 0
    for game in games:
        try:
            a, b = conv.arena_blocks(conv.convert(game), conv.GAMES[game].get("plain", ()))
            print(f"{game}: ok (blocks {a}, {b})")
        except AssertionError as e:
            print(f"{game}: FAIL {e}")
            failed += 1
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main(sys.argv[1:])
