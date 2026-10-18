#!/usr/bin/env python3
# Recompresses the *_packed[] arrays of already generated headers with the
# current asset_emit compressor, without the original ROMs.
#
# Each array is inflated and packed again; the uncompressed bytes must stay
# identical, so the firmware sees the same data. Arrays only shrink: if the
# new stream is not smaller, the old one is kept.
#
# usage: repack_assets.py <header.h>...
import re
import sys
import zlib
from concurrent.futures import ProcessPoolExecutor

import zopfli.zlib

from asset_emit import ZOPFLI_ITERATIONS, _hex_block

ARRAY_RE = re.compile(
    r"// (\w+): \d+ -> \d+ bytes \([\d.-]+% smaller\)\n"
    r"static const unsigned char (\w+)_packed\[\] = \{\n(.*?)\n\};",
    re.S)

def repack(old):
    raw = zlib.decompress(old)
    new = zopfli.zlib.compress(raw, numiterations=ZOPFLI_ITERATIONS)
    if zlib.decompress(new) != raw:
        raise RuntimeError("round-trip mismatch")
    return raw, new if len(new) < len(old) else old

def parse(text):
    """Yield (match, packed bytes) for each array in the header."""
    for m in ARRAY_RE.finditer(text):
        if m.group(1) != m.group(2):
            raise ValueError(f"comment/array name mismatch: {m.group(1)} {m.group(2)}")
        hexs = re.findall(r"0x([0-9A-Fa-f]{2})", m.group(3))
        yield m, bytes(int(h, 16) for h in hexs)

def render(name, raw, packed):
    ratio = 100.0 * (1.0 - len(packed) / len(raw)) if raw else 0.0
    return (f"// {name}: {len(raw)} -> {len(packed)} bytes ({ratio:.1f}% smaller)\n"
            f"static const unsigned char {name}_packed[] = {{\n  " + _hex_block(packed) + "\n};")

def repack_file(path):
    with open(path) as f:
        text = f.read()

    arrays = list(parse(text))
    if not arrays:
        return path, 0, 0

    # rebuild back to front so earlier match offsets stay valid
    old_total = new_total = 0
    for m, old in reversed(arrays):
        raw, new = repack(old)
        old_total += len(old)
        new_total += len(new)
        text = text[:m.start()] + render(m.group(2), raw, new) + text[m.end():]

    with open(path, "w") as f:
        f.write(text)
    return path, old_total, new_total

def main(paths):
    old_sum = new_sum = 0
    with ProcessPoolExecutor() as pool:
        for path, old, new in pool.map(repack_file, paths):
            print(f"{path}: {old} -> {new}")
            old_sum += old
            new_sum += new
    print(f"total: {old_sum} -> {new_sum} bytes, saved {old_sum - new_sum}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("usage: repack_assets.py <header.h>...")
        sys.exit(1)
    main(sys.argv[1:])
