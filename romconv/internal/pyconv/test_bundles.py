#!/usr/bin/env python3
# Checks bundles.py on a temp sidecar tree: base + subclass dirs, a planned
# asset whose remote mode comes from the arena plan header.
import json, os, re, struct, tempfile
from asset_emit import lzma_compress, PLAIN_DICT_BYTES
from bundle import asset_id, bundle_hash, FLAG_LZMA, FLAG_PACKED
import bundles

tmp = tempfile.mkdtemp()
assets, machines, server = (os.path.join(tmp, d) for d in ("assets", "machines", "server"))


def sidecar(dirname, name, raw, mode):
    out = os.path.join(assets, dirname)
    os.makedirs(out, exist_ok=True)
    meta = {"ctype": "unsigned char", "inner": "", "count": len(raw), "mode": mode}
    files = {".json": json.dumps(meta).encode(), ".raw": raw, ".lz": lzma_compress(raw)}
    if mode != "COMPRESSED":
        files[".lz64"] = lzma_compress(raw, PLAIN_DICT_BYTES)
    for ext, data in files.items():
        open(os.path.join(out, name + ext), "wb").write(data)


# pacman: one COMPRESSED, one planned (remote layout PLAIN); mspacman: one PLAIN
sidecar("pacman", "pacman_rom", bytes(range(256)) * 8, "COMPRESSED")
sidecar("pacman", "pacman_tiles", bytes(70000), "PLANNED")
sidecar("mspacman", "mspacman_rom", b"\x05" * 300, "PLAIN")
os.makedirs(os.path.join(machines, "pacman"))
os.makedirs(os.path.join(machines, "mspacman"))
open(os.path.join(machines, "pacman", "pacman_arena_plan.h"), "w").write(
    "#ifdef ASSETS_REMOTE\n#define pacman_tiles_PLAIN 1\n#else\n#define pacman_tiles_PLAIN 0\n#endif\n")

# 1. mspacman bundle: own and base dir assets, planned one as LZMA
info = bundles.make_bundle("mspacman", assets, machines, server)
wire = open(os.path.join(server, "g", info["hex"]), "rb").read()
assert info["hex"] == bundle_hash(wire).hex()
n = struct.unpack_from("<H", wire, 6)[0]
entries = {struct.unpack_from("<5L", wire, 8 + 20 * i)[0]: struct.unpack_from("<5L", wire, 8 + 20 * i)
           for i in range(n)}
assert set(entries) == {asset_id("mspacman_rom"), asset_id("pacman_rom"), asset_id("pacman_tiles")}
assert entries[asset_id("mspacman_rom")][4] == FLAG_LZMA
assert entries[asset_id("pacman_tiles")][4] == FLAG_LZMA
assert entries[asset_id("pacman_rom")][4] == FLAG_PACKED
print("1. bundle contents and flags: OK")

# 2. header: BundleRef with hash, wire and entry length, name; built in -> nullptr
h = open(os.path.join(machines, "mspacman", "mspacman_bundle.h")).read()
hash_c = ", ".join("0x%02x" % b for b in bundle_hash(wire))
ref = ("static constexpr BundleRef mspacman_bundle = { { %s }, %du, %du, \"MSPACMAN\" };"
       % (hash_c, len(wire), info["entry"]))
assert '#include "../../net/bundle.h"' in h and "#ifndef ENABLE_MSPACMAN" in h
assert ref in h, h
assert "#define mspacman_bundle_ref (&mspacman_bundle)" in h
assert "#define mspacman_bundle_ref nullptr" in h
assert f"// GET /g/{info['hex']}" in h
print("2. bundle header: OK")

# 3. digit-led game name: symbol prefixed with _
sidecar("1943", "_1943_rom", b"\x01" * 10, "PLAIN")
os.makedirs(os.path.join(machines, "1943"))
bundles.make_bundle("1943", assets, machines, server)
h = open(os.path.join(machines, "1943", "1943_bundle.h")).read()
assert "static constexpr BundleRef _1943_bundle = " in h and "#define _1943_bundle_ref" in h
assert "#ifndef ENABLE_1943" in h
print("3. digit-led symbol: OK")

# 4. oversize entry fails
sidecar("gng", "gng_huge", bytes(0x100000), "PLAIN")
os.makedirs(os.path.join(machines, "gng"))
try:
    bundles.make_bundle("gng", assets, machines, server)
    raise AssertionError("oversize entry accepted")
except ValueError:
    pass
print("4. oversize entry rejected: OK")

# 5. game without any asset blob (hero: tables only): no bundle, ref nullptr
os.makedirs(os.path.join(machines, "hero"))
info = bundles.make_bundle("hero", assets, machines, server)
h = open(os.path.join(machines, "hero", "hero_bundle.h")).read()
assert info is None and "#define hero_bundle_ref nullptr" in h and "BundleRef" not in h, h
print("5. game without assets: no bundle: OK")
