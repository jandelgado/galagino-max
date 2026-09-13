#!/usr/bin/env python3
# RomData<T> C-source emitter shared by the romconv converters.
import struct
import zlib

_WIDTH = {"unsigned char": 1, "unsigned short": 2, "unsigned long": 4}
_PACK  = {"unsigned char": "B", "unsigned short": "<H", "unsigned long": "<L"}

def _hex_block(data, per_line=16):
    hexs = ["0x{:02X}".format(b) for b in data]
    rows = [",".join(hexs[i:i + per_line]) for i in range(0, len(hexs), per_line)]
    return ",\n  ".join(rows)

def emit_compressed(f, name, ctype, inner_dims, count, flat_values):
    """Write a zlib-compressed RomData<ctype inner_dims> definition.

    flat_values: ints in C initializer (row-major) order.
    """
    width = _WIDTH[ctype]
    raw = b"".join(struct.pack(_PACK[ctype], v & ((1 << (8 * width)) - 1)) for v in flat_values)
    packed = zlib.compress(raw, 9)
    ratio = 100.0 * (1.0 - len(packed) / len(raw)) if raw else 0.0

    print('#include "../../emulation/romdata.h"', file=f)
    print(f"// {name}: {len(raw)} -> {len(packed)} bytes ({ratio:.1f}% smaller)", file=f)
    print(f"static const unsigned char {name}_packed[] = {{\n  " + _hex_block(packed) + "\n};", file=f)
    print(f"static RomData<{ctype}{inner_dims}> {name}({name}_packed, sizeof({name}_packed), {count});", file=f)

def emit_plain(f, name, ctype, inner_dims, count, array_body):
    """Write a plain (flash-resident) RomData<ctype inner_dims> definition.

    array_body: formatted `{ ... }` initializer text from the dump_* helpers.
    """
    print('#include "../../emulation/romdata.h"', file=f)
    print(f"static const {ctype} {name}_data[]{inner_dims} = {{\n{array_body}\n}};", file=f)
    print(f"static RomData<{ctype}{inner_dims}> {name}({name}_data, {count});", file=f)
