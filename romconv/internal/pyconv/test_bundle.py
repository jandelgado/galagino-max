#!/usr/bin/env python3
# Checks bundle.py (remote asset bundles) without a C++ build.
import hashlib, lzma, struct
from bundle import asset_id, BundleWriter, bundle_hash, CACHE_BYTES, TAG_BYTES


def lz(raw, dict_size):
    filters = [{"id": lzma.FILTER_LZMA1, "dict_size": dict_size, "lc": 0, "lp": 0, "pb": 0}]
    return bytes([0]) + lzma.compress(raw, format=lzma.FORMAT_RAW, filters=filters)


# 1. asset id: FNV-1a 32, known vectors
assert asset_id("") == 0x811C9DC5
assert asset_id("a") == 0xE40C292C
print("1. asset_id FNV-1a vectors: OK")

# 2. wire bundle: header v2, PLAIN entries LZMA (flag 2), COMPRESSED packed (flag 1)
plain_raw = b"\x01\x02\x03" * 100
plain_lz = lz(plain_raw, 65536)
comp_raw = b"\x07" * 40
comp_lz = lz(comp_raw, 1 << 20)
w = BundleWriter()
w.add("_a", plain_raw, plain_lz, "PLAIN")
w.add("_b", comp_raw, comp_lz, "COMPRESSED")
data = w.build()
assert data[:8] == struct.pack("<LHH", 0x42474C47, 2, 2)
e0 = struct.unpack_from("<5L", data, 8)
e1 = struct.unpack_from("<5L", data, 28)
assert e0[0] == asset_id("_a") and e0[2:] == (len(plain_lz), len(plain_raw), 2), e0
assert e1[0] == asset_id("_b") and e1[2:] == (len(comp_lz), len(comp_raw), 1), e1
assert e0[1] == 48 and e1[1] % 4 == 0 and e1[1] >= e0[1] + e0[2], (e0, e1)
assert data[e0[1]:e0[1] + e0[2]] == plain_lz
assert data[e1[1]:e1[1] + e1[2]] == comp_lz
assert len(data) == e1[1] + e1[2]
print("2. wire header, entries, alignment: OK")

# 3. cache entry: PLAIN stored raw (flag 0), COMPRESSED copied (flag 1)
entry = w.entry()
assert entry[:8] == struct.pack("<LHH", 0x42474C47, 2, 2)
c0 = struct.unpack_from("<5L", entry, 8)
c1 = struct.unpack_from("<5L", entry, 28)
assert c0 == (asset_id("_a"), 48, len(plain_raw), len(plain_raw), 0), c0
assert c1[0] == asset_id("_b") and c1[2:] == (len(comp_lz), len(comp_raw), 1), c1
assert c1[1] % 4 == 0 and c1[1] >= c0[1] + c0[2]
assert entry[c0[1]:c0[1] + c0[2]] == plain_raw
assert entry[c1[1]:c1[1] + c1[2]] == comp_lz
assert len(entry) == c1[1] + c1[2]
print("3. cache entry layout: OK")

# 4. duplicate name (or FNV collision) rejected: would resolve the wrong asset
w = BundleWriter()
w.add("_a", b"\x01", lz(b"\x01", 4096), "PLAIN")
try:
    w.add("_a", b"\x02", lz(b"\x02", 4096), "PLAIN")
    raise AssertionError("duplicate id accepted")
except ValueError:
    pass
print("4. duplicate id rejected: OK")

# 5. at most 64 entries
w = BundleWriter()
for i in range(64):
    w.add(f"_e{i}", b"\x01", lz(b"\x01", 4096), "PLAIN")
try:
    w.add("_e64", b"\x01", lz(b"\x01", 4096), "PLAIN")
    raise AssertionError("65th entry accepted")
except ValueError:
    pass
print("5. 64 entries max: OK")

# 6. entry + tag sector must fit the cache partition
big = bytes(CACHE_BYTES - TAG_BYTES)
w = BundleWriter()
w.add("_big", big, lz(big, 65536), "PLAIN")
try:
    w.entry()
    raise AssertionError("oversize entry accepted")
except ValueError:
    pass
print("6. oversize entry rejected: OK")

# 7. unknown mode rejected
try:
    BundleWriter().add("_x", b"\x01", b"\x00", "RAW")
    raise AssertionError("unknown mode accepted")
except ValueError:
    pass
print("7. unknown mode rejected: OK")

# 8. bundle hash = first 8 bytes of SHA-256
assert bundle_hash(data) == hashlib.sha256(data).digest()[:8]
print("8. bundle_hash: OK")
