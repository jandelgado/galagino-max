#!/usr/bin/env python3
"""Compare MAME playfield RAM dumps (tools/mame_pf.lua) with decode_level().

usage (romconv/): python3 internal/marble/tools/pf_check.py PFSEQ.BIN
Rows are identified by tile words with bits 12-13 masked; exit 1 on any colour mismatch.
"""
import os
import struct
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from devrom import ROM as rom, md
seq = open(sys.argv[1], "rb").read()
dec = {n: md.decode_level(rom, n) for n in range(md.LEVELS)}
key = lambda row: tuple(w & ~0x3000 for w in row)
idx = defaultdict(list)
for n, m in dec.items():
    for i, row in enumerate(m):
        idx[key(row)].append((n, i))
seen, bad = defaultdict(set), defaultdict(set)
for s in range(0, len(seq), 8192):
    for r in range(64):
        o = s + r * 128 + 6
        row = struct.unpack(">36H", seq[o:o + 72])
        hits = idx.get(key(row), [])
        if len(hits) == 1:
            n, i = hits[0]
            seen[n].add(i)
            if tuple(dec[n][i]) != row:
                bad[n].add(i)
for n in range(md.LEVELS):
    print(f"{md.NAMES[n]}: seen {len(seen[n])}/{len(dec[n])}, colour mismatch {len(bad[n])}")
sys.exit(1 if any(bad.values()) else 0)
