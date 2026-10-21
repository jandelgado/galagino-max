#!/usr/bin/env python3
# Checks the remote form of asset_emit.py: headers written into a machine dir
# carry a remote blob for ASSETS_REMOTE builds without the game built in, and
# leave sidecar files for bundles.py.
import json, lzma, os, re, tempfile
from asset_emit import emit_compressed, emit_plain, lzma_decompress
from bundle import asset_id

tmp = tempfile.mkdtemp()
os.environ["ROMCONV_ASSET_DIR"] = os.path.join(tmp, "assets")


def emit(dirname, fn, *args, **kw):
    path = os.path.join(tmp, "source", "src", "machines", dirname, "x.h")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        fn(f, *args, **kw)
    return open(path).read()


def sidecar(dirname, name, ext):
    path = os.path.join(tmp, "assets", dirname, name + ext)
    return open(path, "rb").read() if os.path.exists(path) else None


def packed_bytes(out, name):
    m = re.search(name + r"_packed\[\] = \{(.*?)\};", out, re.S)
    return bytes(int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", m.group(1)))


def lz64_decode(packed):
    props = packed[0]
    filters = [{"id": lzma.FILTER_LZMA1, "dict_size": 65536, "lc": props % 9,
                "lp": props // 9 % 5, "pb": props // 45}]
    return lzma.decompress(packed[1:], format=lzma.FORMAT_RAW, filters=filters)


# 1. PLAIN: remote blob first, today's array after #else
out = emit("foo", emit_plain, "foo_x", "unsigned char", "", 3, "  0x01,0x02,0x03")
remote = "static constexpr FlashAsset<unsigned char, PLAIN> foo_x_blob = { nullptr, 3, 0x%08Xu };" % asset_id("foo_x")
local = "static constexpr FlashAsset<unsigned char, PLAIN> foo_x_blob = { foo_x_data, 3 };"
guard = "#if defined(ASSETS_REMOTE) && !defined(BUILTIN_FOO)"
assert guard in out and remote in out and local in out, out
assert out.index(guard) < out.index(remote) < out.index("#else") < out.index(local) < out.index("#endif")
meta = json.loads(sidecar("foo", "foo_x", ".json"))
assert meta == {"ctype": "unsigned char", "inner": "", "count": 3, "mode": "PLAIN"}, meta
assert sidecar("foo", "foo_x", ".raw") == b"\x01\x02\x03"
assert lz64_decode(sidecar("foo", "foo_x", ".lz64")) == b"\x01\x02\x03"
assert lzma_decompress(sidecar("foo", "foo_x", ".lz")) == b"\x01\x02\x03"  # ratio report
print("1. emit_plain remote form + sidecars: OK")

# 2. COMPRESSED: remote blob carries the packed length; .lz == emitted array
values = list(range(200)) * 3
out = emit("foo", emit_compressed, "foo_c", "unsigned char", "", len(values), values)
packed = packed_bytes(out, "foo_c")
remote = ("static constexpr FlashAsset<unsigned char, COMPRESSED> foo_c_blob = "
          "{ nullptr, %d, %d, ARENA_ANY, 0x%08Xu };" % (len(packed), len(values), asset_id("foo_c")))
assert remote in out, out
assert sidecar("foo", "foo_c", ".lz") == packed
assert sidecar("foo", "foo_c", ".raw") == bytes(values)
assert sidecar("foo", "foo_c", ".lz64") is None
assert json.loads(sidecar("foo", "foo_c", ".json"))["mode"] == "COMPRESSED"
print("2. emit_compressed remote form + sidecars: OK")

# 3. planned: remote form picked by <name>_PLAIN, mode PLANNED
os.environ["ROMCONV_PLAN"] = "foo"
out = emit("foo", emit_compressed, "foo_p", "unsigned short", "", 3, [1, 2, 0xFFFF])
del os.environ["ROMCONV_PLAN"]
packed = packed_bytes(out, "foo_p")
r_plain = "static constexpr FlashAsset<unsigned short, PLAIN> foo_p_blob = { nullptr, 3, 0x%08Xu };" % asset_id("foo_p")
r_comp = ("static constexpr FlashAsset<unsigned short, COMPRESSED> foo_p_blob = "
          "{ nullptr, %d, 3, foo_p_BLOCK, 0x%08Xu };" % (len(packed), asset_id("foo_p")))
assert r_plain in out and r_comp in out, out
assert out.index(guard) < out.index(r_plain) < out.index(r_comp) < out.index("alignas(4)")
assert json.loads(sidecar("foo", "foo_p", ".json"))["mode"] == "PLANNED"
assert sidecar("foo", "foo_p", ".lz") == packed
assert lz64_decode(sidecar("foo", "foo_p", ".lz64")) == bytes([1, 0, 2, 0, 0xFF, 0xFF])
print("3. planned remote form + sidecars: OK")

# 4. remote=False (logos) and shared dirs: no remote form, no sidecar
out = emit("foo", emit_compressed, "foo_logo", "unsigned char", "", 2, [1, 2], remote=False)
assert "ASSETS_REMOTE" not in out and sidecar("foo", "foo_logo", ".json") is None
out = emit("namco54", emit_compressed, "namco54_rom", "unsigned char", "", 2, [1, 2])
assert "ASSETS_REMOTE" not in out and sidecar("namco54", "namco54_rom", ".json") is None
print("4. remote=False and shared dirs stay built in: OK")
