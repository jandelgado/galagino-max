#!/usr/bin/env python3
# Checks the planned form of asset_emit.py (ROMCONV_PLAN set): both forms
# behind <name>_PLAIN, marker for arena_plan.py, alias per mode.
import io, os, re, struct
import asset_emit
from asset_emit import emit_compressed, emit_plain, lzma_decompress


def packed_of(out, name):
    m = re.search(name + r"_packed\[\] = \{(.*?)\};", out, re.S)
    return lzma_decompress(bytes(int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", m.group(1))))


def plain_values(out, name):
    m = re.search(name + r"_data\[\][^=]*= \{(.*?)\};", out, re.S)
    return [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", m.group(1))]


# reference output without a plan
f = io.StringIO()
emit_plain(f, "_r", "uint32_t", "[2]", 2, " { 0x1,0x2 },\n { 0x3,0x4 }")
ref_plain = f.getvalue()
f = io.StringIO()
emit_compressed(f, "_r", "unsigned short", "", 3, [1, 2, 0xFFFF])
ref_comp = f.getvalue()

os.environ["ROMCONV_PLAN"] = "tst"

# 1. emit_plain: both forms, values parsed from the body, comment ignored
f = io.StringIO()
emit_plain(f, "_p", "uint32_t", "[2]", 2, " { 0x1,0x2 },\n { 0x3,0x4 } // 99")
out = f.getvalue()
for s in ('#include "../../emulation/asset.h"', '#include "tst_arena_plan.h"',
          "// arena-asset _p 16 4 PLAIN uint32_t[2]", "#if _p_PLAIN", "#else", "#endif",
          "static const uint32_t _p_data[][2] = {",
          "static constexpr FlashAsset<uint32_t[2], PLAIN> _p_blob = { _p_data, 2 };",
          "static constexpr FlashAsset<uint32_t[2], COMPRESSED> _p_blob = { _p_packed, sizeof(_p_packed), 2, _p_BLOCK };",
          ):
    assert s in out, (s, out)
assert "using _p_asset" not in out  # the alias lives in the plan header
assert packed_of(out, "_p") == struct.pack("<4L", 1, 2, 3, 4)
print("1. emit_plain planned: both forms, marker, alias: OK")

# 2. body with the wrong value count is rejected
try:
    emit_plain(io.StringIO(), "_bad", "uint32_t", "[2]", 2, " { 0x1,0x2 },\n { 0x3 }")
    raise AssertionError("short body accepted")
except ValueError:
    pass
print("2. wrong value count rejected: OK")

# 3. emit_compressed: plain array from the values, marker
f = io.StringIO()
emit_compressed(f, "_c", "unsigned short", "", 3, [1, 2, 0xFFFF])
out = f.getvalue()
assert "// arena-asset _c 6 2 COMPRESSED unsigned short" in out
assert plain_values(out, "_c") == [1, 2, 0xFFFF]
assert packed_of(out, "_c") == struct.pack("<3H", 1, 2, 0xFFFF)
print("3. emit_compressed planned: plain array, marker: OK")

# 4. signed values keep their bit pattern in both forms
f = io.StringIO()
emit_plain(f, "_s", "signed char", "", 3, "  -1, 2, -128")
out = f.getvalue()
assert packed_of(out, "_s") == bytes([0xFF, 0x02, 0x80])
print("4. signed body values: OK")

# 5. no plan: output unchanged
del os.environ["ROMCONV_PLAN"]
f = io.StringIO()
emit_plain(f, "_r", "uint32_t", "[2]", 2, " { 0x1,0x2 },\n { 0x3,0x4 }")
assert f.getvalue() == ref_plain
f = io.StringIO()
emit_compressed(f, "_r", "unsigned short", "", 3, [1, 2, 0xFFFF])
assert f.getvalue() == ref_comp
print("5. without ROMCONV_PLAN unchanged: OK")

# 6. plan=False keeps today's form (ADPCM: count is samples, not bytes)
os.environ["ROMCONV_PLAN"] = "tst"
f = io.StringIO()
emit_plain(f, "_r", "uint32_t", "[2]", 2, " { 0x1,0x2 },\n { 0x3,0x4 }", plan=False)
assert f.getvalue() == ref_plain
f = io.StringIO()
emit_compressed(f, "_r", "unsigned short", "", 3, [1, 2, 0xFFFF], plan=False)
assert f.getvalue() == ref_comp
from adpcm import emit_adpcm
f = io.StringIO()
emit_adpcm(f, "_a", [0, 100, -100, 50])
assert "arena-asset" not in f.getvalue()
del os.environ["ROMCONV_PLAN"]
print("6. plan=False and ADPCM stay unplanned: OK")
