# Writes source/test/test_bundle_unpack/fixture.h: a wire bundle of 3 entries
# (PACKED, LZMA larger than the 64 KB dictionary, tiny LZMA) and the length
# and FNV-1a of the cache entry the device must write for it.
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import PLAIN_DICT_BYTES
from romconv.emit.lzma import lzma_compress
from romconv.emit.text import hex_block
from romconv.emit.write import banner
from romconv.remote.bundle import BundleWriter


def _gen(n: int) -> bytes:
    # same formula as gen() in test_bundle_unpack.cpp
    return bytes((i * 7 + (i >> 11)) & 0xFF for i in range(n))


def _fnv1a(data: bytes) -> int:
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def _convert(ctx: Context) -> None:
    packed_raw = bytes(range(100))
    big = _gen(70000)
    tiny = b"\x01\x02\x03\x04\x05"
    w = BundleWriter()
    w.add("fx_packed", packed_raw, lzma_compress(packed_raw), "COMPRESSED")
    w.add("fx_big", big, lzma_compress(big, PLAIN_DICT_BYTES), "PLAIN")
    w.add("fx_tiny", tiny, lzma_compress(tiny, PLAIN_DICT_BYTES), "PLAIN")
    wire, entry = w.build(), w.entry()
    out = ctx.paths.repo / "source" / "test" / "test_bundle_unpack" / "fixture.h"
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(banner(__file__))
        f.write(f"static const uint8_t wire[{len(wire)}] = {{\n  {hex_block(wire)}\n}};\n")
        f.write(f"static const uint32_t ENTRY_LENGTH = {len(entry)}u;\n")
        f.write(f"static const uint32_t ENTRY_FNV = 0x{_fnv1a(entry):08X}u;\n")
    ctx.log.info(f"fixture: wire {len(wire)} bytes, entry {len(entry)} bytes")


JOB = Job(name="unpack_fixture", convert=_convert, always=True)
