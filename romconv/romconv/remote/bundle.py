# Remote asset bundle: one file per game on galagino-server. The device
# loader streams it, decodes PLAIN entries and writes a cache entry into its
# data partition, then memory-maps that entry.
#
# Layout, little endian, wire bundle and cache entry alike
# (source/src/net/bundle.cpp, bundle_unpack.cpp parse it):
#   header  magic u32, version u16, entry count u16
#   entries id u32, offset u32, stored length u32, raw length u32, flags u32
#   data    per entry at offset (from bundle start), 4-byte aligned, in order
#
# Flags:   wire                          cache entry
#   PLAIN  LZMA, 64 KB dictionary        RAW, stored == raw (read in place)
#   COMP.  PACKED, app's LZMA stream     PACKED, copied (unpacked to Arena)
import hashlib
import struct
from pathlib import Path

_MAGIC = 0x42474C47  # "GLGB"
_VERSION = 2
_HEADER = struct.Struct("<LHH")
_ENTRY = struct.Struct("<5L")
_FLAG_RAW = 0     # stored bytes are the raw asset bytes
FLAG_PACKED = 1  # props byte + LZMA1 stream, unpacked into Arena on use
FLAG_LZMA = 2    # wire only: decoded into a RAW entry by the loader
_ALIGN = 4        # uint32_t assets are read in place
_MAX_ENTRIES = 64
TAG_BYTES = 4096        # cache tag sector before each entry
CACHE_BYTES = 0x100000  # assets partition (source/partitions-assets.csv)


def fnv1a(data: bytes) -> int:
    """FNV-1a 32."""
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def asset_id(name: str) -> int:
    """FNV-1a 32 of the blob symbol name without `_blob`, e.g. `_1943_rom`."""
    return fnv1a(name.encode())


def bundle_hash(data: bytes) -> bytes:
    """Bundle identity: first 8 bytes of SHA-256, also the URL /g/<hex>."""
    return hashlib.sha256(data).digest()[:8]


def _layout(items):
    """Header + table + 4-byte aligned data; items: (id, data, raw len, flags)."""
    table = bytearray(_HEADER.pack(_MAGIC, _VERSION, len(items)))
    data = bytearray()
    start = _HEADER.size + _ENTRY.size * len(items)
    for aid, stored, raw_len, flags in items:
        data += b"\0" * (-(start + len(data)) % _ALIGN)
        table += _ENTRY.pack(aid, start + len(data), len(stored), raw_len, flags)
        data += stored
    return bytes(table + data)


class BundleWriter:
    def __init__(self):
        self.assets = []

    def add(self, name, raw, packed, mode):
        """mode PLAIN: packed = 64 KB dictionary stream; COMPRESSED: the
        app's stream (`<name>_packed`), its length is in the remote blob."""
        if mode not in ("PLAIN", "COMPRESSED"):
            raise ValueError(f"{name}: unknown mode {mode}")
        aid = asset_id(name)
        # duplicate name or FNV collision: the device would resolve the wrong asset
        if any(aid == a[0] for a in self.assets):
            raise ValueError(f"asset id 0x{aid:08X} of {name} already in bundle")
        if len(self.assets) == _MAX_ENTRIES:
            raise ValueError(f"{name}: more than {_MAX_ENTRIES} assets in one bundle")
        self.assets.append((aid, bytes(raw), bytes(packed), mode))

    def build(self):
        """Wire bundle, as served under /g/<hash>."""
        return _layout([(aid, packed, len(raw), FLAG_LZMA if mode == "PLAIN" else FLAG_PACKED)
                        for aid, raw, packed, mode in self.assets])

    def entry(self):
        """Cache entry the device writes for build()."""
        out = _layout([(aid, raw, len(raw), _FLAG_RAW) if mode == "PLAIN"
                       else (aid, packed, len(raw), FLAG_PACKED)
                       for aid, raw, packed, mode in self.assets])
        if len(out) + TAG_BYTES > CACHE_BYTES:
            raise ValueError(f"cache entry {len(out)} bytes does not fit the assets partition")
        return out


def write_bundle(data: bytes, server_root: Path) -> str:
    """Write to <server_root>/g/<hex>, return the hex name."""
    name = bundle_hash(data).hex()
    out = Path(server_root) / "g"
    out.mkdir(parents=True, exist_ok=True)
    (out / name).write_bytes(data)
    return name
