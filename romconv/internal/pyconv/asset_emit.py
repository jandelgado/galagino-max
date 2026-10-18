#!/usr/bin/env python3
# Asset<T> C-source emitter shared by the romconv converters.
#
# Emits the array plus a constexpr FlashAsset `<name>_blob` describing it. The
# machine owns an Asset member built from the blob, so the unpack cache
# dies with the machine. Menu logos wrap the blob in a Logo (logoconv.py).
import lzma
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

def emit_compressed(f, name, ctype, inner_dims, count, flat_values):
    """Write an LZMA-compressed FlashAsset<ctype inner_dims> definition.

    flat_values: ints in C initializer (row-major) order.
    """
    width = _WIDTH[ctype]
    raw = b"".join(struct.pack(_PACK[ctype], v & ((1 << (8 * width)) - 1)) for v in flat_values)
    packed = lzma_compress(raw)
    ratio = 100.0 * (1.0 - len(packed) / len(raw)) if raw else 0.0

    print('#include "../../emulation/asset.h"', file=f)
    print(f"// {name}: {len(raw)} -> {len(packed)} bytes ({ratio:.1f}% smaller)", file=f)
    print(f"static const unsigned char {name}_packed[] = {{\n  " + _hex_block(packed) + "\n};", file=f)
    print(f"static constexpr FlashAsset<{ctype}{inner_dims}, COMPRESSED> {name}_blob = {{ {name}_packed, sizeof({name}_packed), {count} }};", file=f)

def emit_plain(f, name, ctype, inner_dims, count, array_body):
    """Write a plain (flash-resident) FlashAsset<ctype inner_dims> definition.

    array_body: formatted `{ ... }` initializer text from the dump_* helpers.
    """
    print('#include "../../emulation/asset.h"', file=f)
    print(f"static const {ctype} {name}_data[]{inner_dims} = {{\n{array_body}\n}};", file=f)
    print(f"static constexpr FlashAsset<{ctype}{inner_dims}, PLAIN> {name}_blob = {{ {name}_data, {count} }};", file=f)
