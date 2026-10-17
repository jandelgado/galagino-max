#!/usr/bin/env python3
# Checks bundle.py (remote asset bundles) and emit_plain_remote() without a
# C++ build.
import hashlib, io, struct
from bundle import asset_id, BundleWriter, bundle_hash
from asset_emit import emit_plain_remote

# 1. asset id: FNV-1a 32, known vectors
assert asset_id("") == 0x811C9DC5
assert asset_id("a") == 0xE40C292C
print("1. asset_id FNV-1a vectors: OK")

# 2. layout: header, entries, 4-byte aligned data
w = BundleWriter()
w.add("_a", b"\x01\x02\x03")
w.add("_b", b"\x04" * 8)
data = w.build()
assert data[:8] == struct.pack("<LHH", 0x42474C47, 1, 2)
e0 = struct.unpack_from("<5L", data, 8)
e1 = struct.unpack_from("<5L", data, 28)
assert e0 == (asset_id("_a"), 48, 3, 3, 0), e0
assert e1 == (asset_id("_b"), 52, 8, 8, 0), e1
assert data[48:51] == b"\x01\x02\x03"
assert data[52:60] == b"\x04" * 8
assert len(data) == 60
print("2. bundle header, entries, alignment: OK")

# 2b. duplicate name (or FNV collision) rejected: would resolve the wrong asset
w = BundleWriter()
w.add("_a", b"\x01")
try:
    w.add("_a", b"\x02")
    raise AssertionError("duplicate id accepted")
except ValueError:
    pass
print("2b. duplicate id rejected: OK")

# 3. bundle hash = first 8 bytes of SHA-256
assert bundle_hash(data) == hashlib.sha256(data).digest()[:8]
print("3. bundle_hash: OK")

# 4. remote PLAIN header carries both forms
f = io.StringIO()
emit_plain_remote(f, "_test_rom", "unsigned char", "", 3, "  0x01,0x02,0x03")
out = f.getvalue()
blob_remote = "static constexpr FlashAsset<unsigned char, PLAIN> _test_rom_blob = { nullptr, 3, 0x%08Xu };" % asset_id("_test_rom")
blob_local = "static constexpr FlashAsset<unsigned char, PLAIN> _test_rom_blob = { _test_rom_data, 3 };"
assert "#ifdef ASSETS_REMOTE" in out
assert blob_remote in out, out
assert "#else" in out
assert "static const unsigned char _test_rom_data[] = {" in out
assert blob_local in out
assert out.index(blob_remote) < out.index("#else") < out.index(blob_local) < out.index("#endif")
print("4. emit_plain_remote both forms: OK")
