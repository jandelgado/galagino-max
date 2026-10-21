import re
import struct
from collections.abc import Sequence

WIDTH = {"unsigned char": 1, "signed char": 1, "unsigned short": 2, "uint32_t": 4}
_PACK = {"unsigned char": "B", "signed char": "B", "unsigned short": "<H", "uint32_t": "<L"}


def hex_block(data: bytes, per_line: int = 16) -> str:
    hexs = ["0x{:02X}".format(b) for b in data]
    rows = [",".join(hexs[i:i + per_line]) for i in range(0, len(hexs), per_line)]
    return ",\n  ".join(rows)


def raw_bytes(ctype: str, values: Sequence[int]) -> bytes:
    width = WIDTH[ctype]
    return b"".join(struct.pack(_PACK[ctype], v & ((1 << (8 * width)) - 1)) for v in values)


def body_values(array_body: str, expected: int | None = None) -> list[int]:
    """Ints of a dump_* initializer text; comments ignored."""
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", array_body, flags=re.S)
    values = [int(v, 0) for v in re.findall(r"-?(?:0x[0-9A-Fa-f]+|\d+)", text)]
    if expected is not None and len(values) != expected:
        raise ValueError(f"array body has {len(values)} values, expected {expected}")
    return values


def flat_body(ctype: str, values: Sequence[int]) -> str:
    """C initializer for flat values (brace elision covers inner dims)."""
    width = 8 * WIDTH[ctype]
    mask = (1 << width) - 1
    if ctype.startswith("signed"):
        vals = [str(((v & mask) ^ (1 << (width - 1))) - (1 << (width - 1))) for v in values]
    else:
        vals = ["0x{:X}".format(v & mask) for v in values]
    rows = [",".join(vals[i:i + 16]) for i in range(0, len(vals), 16)]
    return "  " + ",\n  ".join(rows)
