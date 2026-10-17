#!/usr/bin/env python3
# Asset<T> C-source emitter shared by the romconv converters.
#
# Emits the array plus a constexpr FlashAsset `<name>_blob` describing it. The
# machine owns an Asset member built from the blob, so the unpack cache
# dies with the machine. Menu logos wrap the blob in a Logo (logoconv.py).
import lzma
import os
import re
import struct

# Raw LZMA1 stream, decoded on the ESP32 by LzmaDec into the Arena. lc=lp=0
# keeps the decoder's probability table at 5.5 KB (free lc/lp: up to 28 KB,
# saves only ~5 KB flash over all assets). pb does not change decoder RAM,
# so it is searched per asset.
LZMA_PB_RANGE = range(5)

_WIDTH = {"unsigned char": 1, "signed char": 1, "unsigned short": 2, "uint32_t": 4}
_PACK  = {"unsigned char": "B", "signed char": "B", "unsigned short": "<H", "uint32_t": "<L"}

def _hex_block(data, per_line=16):
    hexs = ["0x{:02X}".format(b) for b in data]
    rows = [",".join(hexs[i:i + per_line]) for i in range(0, len(hexs), per_line)]
    return ",\n  ".join(rows)

def _lzma_props_byte(lc, lp, pb):
    return (pb * 5 + lp) * 9 + lc

def lzma_compress(raw):
    """Properties byte + raw LZMA1 stream (with end marker), smallest pb."""
    best = None
    for pb in LZMA_PB_RANGE:
        filters = [{"id": lzma.FILTER_LZMA1, "preset": 9 | lzma.PRESET_EXTREME,
                    "dict_size": max(len(raw), 4096), "lc": 0, "lp": 0, "pb": pb}]
        stream = lzma.compress(raw, format=lzma.FORMAT_RAW, filters=filters)
        if best is None or len(stream) < len(best):
            best = bytes([_lzma_props_byte(0, 0, pb)]) + stream
    if lzma_decompress(best) != raw:
        raise RuntimeError("lzma round-trip mismatch")
    return best

def lzma_decompress(packed):
    """Inverse of lzma_compress()."""
    props = packed[0]
    lc, rest = props % 9, props // 9
    lp, pb = rest % 5, rest // 5
    # dictionary only needs to cover the largest asset
    filters = [{"id": lzma.FILTER_LZMA1, "dict_size": 1 << 22, "lc": lc, "lp": lp, "pb": pb}]
    return lzma.decompress(packed[1:], format=lzma.FORMAT_RAW, filters=filters)

def _raw(ctype, flat_values):
    width = _WIDTH[ctype]
    return b"".join(struct.pack(_PACK[ctype], v & ((1 << (8 * width)) - 1)) for v in flat_values)

def _inner_elements(inner_dims):
    n = 1
    for d in re.findall(r"\d+", inner_dims):
        n *= int(d)
    return n

def _body_values(array_body, expected):
    """Ints of a dump_* initializer text; comments ignored."""
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", array_body, flags=re.S)
    values = [int(v, 0) for v in re.findall(r"-?(?:0x[0-9A-Fa-f]+|\d+)", text)]
    if len(values) != expected:
        raise ValueError(f"array body has {len(values)} values, expected {expected}")
    return values

def _flat_body(ctype, flat_values):
    """C initializer for flat values (brace elision covers inner dims)."""
    width = 8 * _WIDTH[ctype]
    mask = (1 << width) - 1
    if ctype.startswith("signed"):
        vals = [str(((v & mask) ^ (1 << (width - 1))) - (1 << (width - 1))) for v in flat_values]
    else:
        vals = ["0x{:X}".format(v & mask) for v in flat_values]
    rows = [",".join(vals[i:i + 16]) for i in range(0, len(vals), 16)]
    return "  " + ",\n  ".join(rows)

def _emit_planned(f, game, name, ctype, inner_dims, count, raw, array_body, hint):
    """Both forms, picked by <name>_PLAIN from <game>_arena_plan.h
    (romconv arena_plan.py), plus the marker arena_plan.py reads. hint: the
    converter's own mode, arena_plan.py's tie-break."""
    packed = lzma_compress(raw)
    ratio = 100.0 * (1.0 - len(packed) / len(raw)) if raw else 0.0
    t = f"{ctype}{inner_dims}"
    print('#include "../../emulation/asset.h"', file=f)
    print(f'#include "{game}_arena_plan.h"', file=f)
    print(f"// arena-asset {name} {len(raw)} {_WIDTH[ctype]} {hint}", file=f)
    print(f"#if {name}_PLAIN", file=f)
    print(f"static const {ctype} {name}_data[]{inner_dims} = {{\n{array_body}\n}};", file=f)
    print(f"static constexpr FlashAsset<{t}, PLAIN> {name}_blob = {{ {name}_data, {count} }};", file=f)
    print(f"using {name}_asset = Asset<{t}, PLAIN>;", file=f)
    print("#else", file=f)
    print(f"// {name}: {len(raw)} -> {len(packed)} bytes ({ratio:.1f}% smaller)", file=f)
    print(f"static const unsigned char {name}_packed[] = {{\n  " + _hex_block(packed) + "\n};", file=f)
    print(f"static constexpr FlashAsset<{t}, COMPRESSED> {name}_blob = {{ {name}_packed, sizeof({name}_packed), {count}, {name}_BLOCK }};", file=f)
    print(f"using {name}_asset = Asset<{t}, COMPRESSED>;", file=f)
    print("#endif", file=f)

def emit_compressed(f, name, ctype, inner_dims, count, flat_values, plan=True):
    """Write an LZMA-compressed FlashAsset<ctype inner_dims> definition.

    flat_values: ints in C initializer (row-major) order.
    ROMCONV_PLAN=<game> set and plan: planned form, both PLAIN and COMPRESSED.
    """
    raw = _raw(ctype, flat_values)
    game = plan and os.environ.get("ROMCONV_PLAN")
    if game:
        _emit_planned(f, game, name, ctype, inner_dims, count, raw, _flat_body(ctype, flat_values),
                      "COMPRESSED")
        return
    packed = lzma_compress(raw)
    ratio = 100.0 * (1.0 - len(packed) / len(raw)) if raw else 0.0

    print('#include "../../emulation/asset.h"', file=f)
    print(f"// {name}: {len(raw)} -> {len(packed)} bytes ({ratio:.1f}% smaller)", file=f)
    print(f"static const unsigned char {name}_packed[] = {{\n  " + _hex_block(packed) + "\n};", file=f)
    print(f"static constexpr FlashAsset<{ctype}{inner_dims}, COMPRESSED> {name}_blob = {{ {name}_packed, sizeof({name}_packed), {count} }};", file=f)

def emit_plain_remote(f, name, ctype, inner_dims, count, array_body):
    """Like emit_plain(), plus the ASSETS_REMOTE form: no array, the blob
    carries the asset id and Asset<PLAIN> resolves it in the mounted bundle.
    """
    from bundle import asset_id
    print('#include "../../emulation/asset.h"', file=f)
    print("#ifdef ASSETS_REMOTE", file=f)
    print(f"static constexpr FlashAsset<{ctype}{inner_dims}, PLAIN> {name}_blob = {{ nullptr, {count}, 0x{asset_id(name):08X}u }};", file=f)
    print("#else", file=f)
    print(f"static const {ctype} {name}_data[]{inner_dims} = {{\n{array_body}\n}};", file=f)
    print(f"static constexpr FlashAsset<{ctype}{inner_dims}, PLAIN> {name}_blob = {{ {name}_data, {count} }};", file=f)
    print("#endif", file=f)

def emit_plain(f, name, ctype, inner_dims, count, array_body, plan=True):
    """Write a plain (flash-resident) FlashAsset<ctype inner_dims> definition.

    array_body: formatted `{ ... }` initializer text from the dump_* helpers.
    ROMCONV_PLAN=<game> set and plan: planned form, values parsed back from
    the body.
    """
    game = plan and os.environ.get("ROMCONV_PLAN")
    if game:
        values = _body_values(array_body, count * _inner_elements(inner_dims))
        _emit_planned(f, game, name, ctype, inner_dims, count, _raw(ctype, values), array_body, "PLAIN")
        return
    print('#include "../../emulation/asset.h"', file=f)
    print(f"static const {ctype} {name}_data[]{inner_dims} = {{\n{array_body}\n}};", file=f)
    print(f"static constexpr FlashAsset<{ctype}{inner_dims}, PLAIN> {name}_blob = {{ {name}_data, {count} }};", file=f)
