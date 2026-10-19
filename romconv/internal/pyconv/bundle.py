#!/usr/bin/env python3
# Remote asset bundle: one file per game on galagino-server, downloaded by
# the device loader into its data partition and memory-mapped.
#
# Layout, little endian (source/src/net/bundle.cpp parses it):
#   header  magic u32, version u16, entry count u16
#   entries id u32, offset u32, stored length u32, raw length u32, flags u32
#   data    per entry at offset (from bundle start), 4-byte aligned
import hashlib
import os
import struct

MAGIC = 0x42474C47  # "GLGB"
VERSION = 1
HEADER = struct.Struct("<LHH")
ENTRY = struct.Struct("<5L")
FLAG_RAW = 0  # stored bytes are the raw asset bytes
ALIGN = 4     # uint32_t assets are read in place


def asset_id(name):
    """FNV-1a 32 of the blob symbol name without `_blob`, e.g. `_1943_rom`."""
    h = 0x811C9DC5
    for b in name.encode():
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def bundle_hash(data):
    """Bundle identity: first 8 bytes of SHA-256, also the URL /g/<hex>."""
    return hashlib.sha256(data).digest()[:8]


class BundleWriter:
    def __init__(self):
        self.assets = []

    def add(self, name, raw):
        aid = asset_id(name)
        # duplicate name or FNV collision: the device would resolve the wrong asset
        if any(aid == other for other, _ in self.assets):
            raise ValueError(f"asset id 0x{aid:08X} of {name} already in bundle")
        self.assets.append((aid, bytes(raw)))

    def build(self):
        table = bytearray(HEADER.pack(MAGIC, VERSION, len(self.assets)))
        data = bytearray()
        start = HEADER.size + ENTRY.size * len(self.assets)
        for aid, raw in self.assets:
            data += b"\0" * (-(start + len(data)) % ALIGN)
            table += ENTRY.pack(aid, start + len(data), len(raw), len(raw), FLAG_RAW)
            data += raw
        return bytes(table + data)


def write_bundle(data, server_root):
    """Write to <server_root>/g/<hex>, return the hex name."""
    name = bundle_hash(data).hex()
    out = os.path.join(server_root, "g")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, name), "wb") as f:
        f.write(data)
    return name
