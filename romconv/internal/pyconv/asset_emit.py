#!/usr/bin/env python3
# Asset<T> C-source emitter shared by the romconv converters.
#
# Emits the array plus a constexpr FlashAsset `<name>_blob` describing it. The
# machine owns an Asset member built from the blob, so the unpack cache
# dies with the machine. Menu logos wrap the blob in a Logo (logoconv.py).
#
# Headers written into source/src/machines/<dir>/ also get a remote form
# (ASSETS_REMOTE build without the game built in): the blob carries the asset
# id, Asset resolves it in the mounted bundle. Sidecar files feed bundles.py.
#
#   #if defined(ASSETS_REMOTE) && !defined(BUILTIN_<DIR>)
#   <name>_blob = { nullptr, ..., <id> }      remote
#   #else
#   <name>_data / <name>_packed, <name>_blob  built in (today's form)
#   #endif
import json
import lzma
import os
import re
import struct

from bundle import asset_id

# Raw LZMA1 stream, decoded on the ESP32 by LzmaDec into the Arena. lc=lp=0
# keeps the decoder's probability table at 5.5 KB (free lc/lp: up to 28 KB,
# saves only ~5 KB flash over all assets). pb does not change decoder RAM,
# so it is searched per asset.
LZMA_PB_RANGE = range(5)

# Dictionary of PLAIN assets in remote bundles: the loader decodes them into
# a dictionary buffer in Arena (source/src/net/bundle_unpack.h).
PLAIN_DICT_BYTES = 65536

# Machine dirs whose assets several games share: always built in
SHARED_DIRS = {"namco54", "williams"}

# romconv/build/assets/<dir>/<name>.{json,raw,lz,lz64}
ASSET_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build", "assets")

_WIDTH = {"unsigned char": 1, "signed char": 1, "unsigned short": 2, "uint32_t": 4}
_PACK  = {"unsigned char": "B", "signed char": "B", "unsigned short": "<H", "uint32_t": "<L"}

def _hex_block(data, per_line=16):
    hexs = ["0x{:02X}".format(b) for b in data]
    rows = [",".join(hexs[i:i + per_line]) for i in range(0, len(hexs), per_line)]
    return ",\n  ".join(rows)

def _lzma_props_byte(lc, lp, pb):
    return (pb * 5 + lp) * 9 + lc

def lzma_compress(raw, dict_size=None):
    """Properties byte + raw LZMA1 stream (with end marker), smallest pb.
    dict_size None: the whole asset (decoded in place)."""
    dict_bytes = max(len(raw), 4096)
    if dict_size:
        dict_bytes = min(dict_size, dict_bytes)
    best = None
    for pb in LZMA_PB_RANGE:
        filters = [{"id": lzma.FILTER_LZMA1, "preset": 9 | lzma.PRESET_EXTREME,
                    "dict_size": dict_bytes, "lc": 0, "lp": 0, "pb": pb}]
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

def _body_values(array_body, expected=None):
    """Ints of a dump_* initializer text; comments ignored."""
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", array_body, flags=re.S)
    values = [int(v, 0) for v in re.findall(r"-?(?:0x[0-9A-Fa-f]+|\d+)", text)]
    if expected is not None and len(values) != expected:
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

def _remote_dir(f, remote):
    """Machine dir of the header f writes, if it gets a remote form."""
    path = getattr(f, "name", None)
    if not remote or not isinstance(path, str):
        return None
    d = os.path.dirname(os.path.abspath(path))
    if os.path.basename(os.path.dirname(d)) != "machines":
        return None
    name = os.path.basename(d)
    return None if name in SHARED_DIRS else name

def _write_sidecar(rdir, name, ctype, inner_dims, count, mode, raw, packed):
    """Inputs of bundles.py: the raw bytes and the stored forms."""
    out = os.path.join(os.environ.get("ROMCONV_ASSET_DIR", ASSET_DIR), rdir)
    os.makedirs(out, exist_ok=True)
    meta = {"ctype": ctype, "inner": inner_dims, "count": count, "mode": mode}
    files = {".json": json.dumps(meta).encode(), ".raw": raw}
    if packed is not None:
        files[".lz"] = packed
    if mode != "COMPRESSED":
        files[".lz64"] = lzma_compress(raw, PLAIN_DICT_BYTES)
    for ext in (".lz", ".lz64"):
        if ext not in files and os.path.exists(os.path.join(out, name + ext)):
            os.remove(os.path.join(out, name + ext))
    for ext, data in files.items():
        with open(os.path.join(out, name + ext), "wb") as g:
            g.write(data)

def _remote_open(f, rdir, name, ctype, inner_dims, count, mode, raw, packed):
    """Remote blob(s) and the #else of the built-in form; mode PLANNED: both
    blobs behind <name>_PLAIN from the arena plan header."""
    t = f"{ctype}{inner_dims}"
    aid = asset_id(name)
    plain = f"static constexpr FlashAsset<{t}, PLAIN> {name}_blob = {{ nullptr, {count}, 0x{aid:08X}u }};"
    block = f"{name}_BLOCK" if mode == "PLANNED" else "ARENA_ANY"
    packed_len = len(packed) if packed is not None else 0
    comp = (f"static constexpr FlashAsset<{t}, COMPRESSED> {name}_blob = "
            f"{{ nullptr, {packed_len}, {count}, {block}, 0x{aid:08X}u }};")
    print(f"#if defined(ASSETS_REMOTE) && !defined(BUILTIN_{rdir.upper()})", file=f)
    if mode == "PLANNED":
        print(f"#if {name}_PLAIN\n{plain}\n#else\n{comp}\n#endif", file=f)
    else:
        print(plain if mode == "PLAIN" else comp, file=f)
    print("#else", file=f)
    _write_sidecar(rdir, name, ctype, inner_dims, count, mode, raw, packed)

def _remote_close(f, rdir):
    if rdir:
        print("#endif", file=f)

def _emit_planned(f, game, name, ctype, inner_dims, count, raw, array_body, hint, rdir):
    """Both forms, picked by <name>_PLAIN from <game>_arena_plan.h
    (romconv arena_plan.py, also defines <name>_asset), plus the marker
    arena_plan.py reads. hint: the converter's own mode, the tie-break."""
    packed = lzma_compress(raw)
    ratio = 100.0 * (1.0 - len(packed) / len(raw)) if raw else 0.0
    t = f"{ctype}{inner_dims}"
    print('#include "../../emulation/asset.h"', file=f)
    print(f'#include "{game}_arena_plan.h"', file=f)
    print(f"// arena-asset {name} {len(raw)} {_WIDTH[ctype]} {hint} {t}", file=f)
    if rdir:
        _remote_open(f, rdir, name, ctype, inner_dims, count, "PLANNED", raw, packed)
    print(f"#if {name}_PLAIN", file=f)
    # aligned like an Arena copy: readers may load wider words (marble_tables)
    print(f"alignas(4) static const {ctype} {name}_data[]{inner_dims} = {{\n{array_body}\n}};", file=f)
    print(f"static constexpr FlashAsset<{t}, PLAIN> {name}_blob = {{ {name}_data, {count} }};", file=f)
    print("#else", file=f)
    print(f"// {name}: {len(raw)} -> {len(packed)} bytes ({ratio:.1f}% smaller)", file=f)
    print(f"static const unsigned char {name}_packed[] = {{\n  " + _hex_block(packed) + "\n};", file=f)
    print(f"static constexpr FlashAsset<{t}, COMPRESSED> {name}_blob = {{ {name}_packed, sizeof({name}_packed), {count}, {name}_BLOCK }};", file=f)
    print("#endif", file=f)
    _remote_close(f, rdir)

def emit_compressed(f, name, ctype, inner_dims, count, flat_values, plan=True, remote=True):
    """Write an LZMA-compressed FlashAsset<ctype inner_dims> definition.

    flat_values: ints in C initializer (row-major) order.
    ROMCONV_PLAN=<game> set and plan: planned form, both PLAIN and COMPRESSED.
    remote: remote form too, if f writes into a machine dir (not for logos).
    """
    raw = _raw(ctype, flat_values)
    rdir = _remote_dir(f, remote)
    game = plan and os.environ.get("ROMCONV_PLAN")
    if game:
        _emit_planned(f, game, name, ctype, inner_dims, count, raw, _flat_body(ctype, flat_values),
                      "COMPRESSED", rdir)
        return
    packed = lzma_compress(raw)
    ratio = 100.0 * (1.0 - len(packed) / len(raw)) if raw else 0.0

    print('#include "../../emulation/asset.h"', file=f)
    if rdir:
        _remote_open(f, rdir, name, ctype, inner_dims, count, "COMPRESSED", raw, packed)
    print(f"// {name}: {len(raw)} -> {len(packed)} bytes ({ratio:.1f}% smaller)", file=f)
    print(f"static const unsigned char {name}_packed[] = {{\n  " + _hex_block(packed) + "\n};", file=f)
    print(f"static constexpr FlashAsset<{ctype}{inner_dims}, COMPRESSED> {name}_blob = {{ {name}_packed, sizeof({name}_packed), {count} }};", file=f)
    _remote_close(f, rdir)

def emit_plain(f, name, ctype, inner_dims, count, array_body, plan=True, remote=True):
    """Write a plain (flash-resident) FlashAsset<ctype inner_dims> definition.

    array_body: formatted `{ ... }` initializer text from the dump_* helpers.
    ROMCONV_PLAN=<game> set and plan: planned form, values parsed back from
    the body.
    remote: remote form too, if f writes into a machine dir.
    """
    rdir = _remote_dir(f, remote)
    game = plan and os.environ.get("ROMCONV_PLAN")
    if game:
        values = _body_values(array_body, count * _inner_elements(inner_dims))
        _emit_planned(f, game, name, ctype, inner_dims, count, _raw(ctype, values), array_body, "PLAIN",
                      rdir)
        return
    print('#include "../../emulation/asset.h"', file=f)
    if rdir:
        # count is not always the byte count (ADPCM: samples), so take all values
        raw = _raw(ctype, _body_values(array_body))
        _remote_open(f, rdir, name, ctype, inner_dims, count, "PLAIN", raw, None)
    print(f"static const {ctype} {name}_data[]{inner_dims} = {{\n{array_body}\n}};", file=f)
    print(f"static constexpr FlashAsset<{ctype}{inner_dims}, PLAIN> {name}_blob = {{ {name}_data, {count} }};", file=f)
    _remote_close(f, rdir)
