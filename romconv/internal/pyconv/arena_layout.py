#!/usr/bin/env python3
# Arena block sizes per layout, read from source/src/emulation/arena_layout.h
# (the C++ side uses the same file). An allocation cannot span blocks.
import os
import re

HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "..", "..", "source", "src", "emulation", "arena_layout.h")


def read(path=HEADER):
    """{"local": (a, b), "remote": (a, b)}; ValueError if a define is missing."""
    with open(path) as f:
        defs = dict(re.findall(r"^#define (ARENA_\w+) (\d+)$", f.read(), re.M))
    try:
        b = int(defs["ARENA_B_SIZE"])
        return {"local": (int(defs["ARENA_A_LOCAL"]), b),
                "remote": (int(defs["ARENA_A_REMOTE"]), b)}
    except KeyError as e:
        raise ValueError(f"{path}: missing #define {e.args[0]}")


LAYOUTS = read()
LOCAL = LAYOUTS["local"]
REMOTE = LAYOUTS["remote"]
