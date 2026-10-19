#!/usr/bin/env python3
# Checks arena_layout.py (Arena block sizes shared with arena.h).
import os, tempfile
from arena_layout import read, LOCAL, REMOTE, LAYOUTS

assert LOCAL == (91000, 110500) and REMOTE == (69000, 110500)
assert LAYOUTS == {"local": LOCAL, "remote": REMOTE}
print("1. layouts from arena_layout.h: OK")

# a header missing a define is an error, not a silent default
d = tempfile.mkdtemp()
p = os.path.join(d, "h.h")
with open(p, "w") as f:
    f.write("#define ARENA_A_LOCAL 91000\n#define ARENA_B_SIZE 110500\n")
try:
    read(p)
    raise AssertionError("missing ARENA_A_REMOTE accepted")
except ValueError:
    pass
print("2. missing define rejected: OK")
