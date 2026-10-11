#!/usr/bin/env python3
"""Marble Madness (MAME `marble`, set 1) ROM decode, shared by the converter and tools.

Facts and addresses: MARBLE.md. Cross-checked against Marble Love
(github.com/magno73/marble-love, MIT) where noted there.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "pyconv"))
from gfxutil import load_file

HERE = os.path.dirname(os.path.abspath(__file__))
ZIP = os.path.join(HERE, "..", "..", "..", "romszip", "marble.zip")

LEVELS = 6
COLS = 36  # playfield width in tiles, 288 px
SCREEN_ROWS = 30  # 240 px visible below the camera
NAMES = ["practice", "beginner", "intermediate", "aerial", "silly", "ultimate"]
BANK = [1, 2, 2, 3, 3, 0]  # slapstic bank holding each level's bitstream

LEVEL_TABLE = 0x2BE00   # 6 x long: level descriptor
ROW_STREAM = 0x800E4    # bitstream base, + signed row offset
ATTR_RUNS = 0x2BE18     # attribute runs, + per-row index byte
CODE_TABLE = 0x2499A    # 32 words, 9 bit codes
RUN_TABLE = 0x249DA     # 8 words, run codes 1-4
PAL_SETS = 0x1FC10      # 6 x 195 words
PAL_TARGETS = 0x20534   # 195 x long: palette RAM address per word
PAL_ALPHA = 0x1FBD0     # 32 words, alpha palette

SHA1 = {
    "136033.623": "a24d2fd587dffcc8536ef28fcbcf5c964a6b67a9",
    "136033.624": "978b1565da746f7389eaf7646604990fb28d47ed",
    "136033.625": "a444b72ff4cdecee3b9dd7e636d658c31ecc186c",
    "136033.626": "d6059c1fe13f28ada27f6586215a16e2117e3ecd",
    "136033.627": "cefc0fa9c71512c961272fcf0f9c069f1396468e",
    "136033.628": "b007d9e45a1442ab1c9ec1463f9f46ea85fb0659",
    "136033.229": "0464ea183685de83e797b9d946b4acc409f4c451",
    "136033.630": "95e31acf29cd8d51beefa9b0e4acd92b81980c2f",
    "136033.107": "4754eac5e6d8547b3ee00f3f48eaa560eb403862",
    "136033.108": "37d51a9e9cb33d1156d02a312ac8e202a18d7c20",
    "136033.421": "d4e68226b87df8834dc3d6daa9d683f17896c32e",
    "136033.422": "49acb9443c5d2c1016cde7f489deab2575dd82ca",
    "136033.137": "b826a178660ff2e278558e4779586737751dca5e",
    "136033.138": "238a913529781f424a6f49c3fba1524684d0cbcf",
    "136033.139": "987a8289fd4be06b6899bb8c620ddfa4c4b966b0",
    "136033.140": "25538e98e5fdadfa6de3bd57ee8658c7add0169d",
    "136033.141": "e2ce22f89304b2d6858d0c61040d8ff2ee33347f",
    "136033.142": "d8e4d6dbbef3be86558ab23646c77400793a5eb1",
    "136033.143": "361e024e1173299d0e6b776a7c1be10767cfab0d",
    "136033.144": "642e469ce10067502ce3a920066184a9c73a44c5",
    "136033.145": "ae372433da441b69345d67f5e838b8479557517b",
    "136033.146": "7134c233daba3b2510d1c08d9efde79c0eec6c20",
    "136033.149": "e719d956f4f9d703a12e2c5520cac0a2f47ea058",
    "136033.151": "5192c0a2887f46b616d130bdbfffbbd5e394e9a3",
    "136033.153": "33c7a38c66fb4d67a6ee88ef2da2bba091439e0c",
    "136033.118": "e4fb8dfa80ed78847c697f9de2bd8540b0c04889",
    "136033.119": "041f24cc03c9043c31c3294c9565dfda9bdada74",
}


class Rom:
    pass


def _f(name):
    return bytes(load_file(ZIP, [name], SHA1[name]))


def _interleave(even, odd):
    return bytes(b for pair in zip(even, odd) for b in pair)


def _inv(b):
    return bytes(x ^ 0xFF for x in b)


def load():
    r = Rom()
    r.prog = {
        0x10000: _interleave(_f("136033.623"), _f("136033.624")),
        0x18000: _interleave(_f("136033.625"), _f("136033.626")),
        0x20000: _interleave(_f("136033.627"), _f("136033.628")),
        0x28000: _interleave(_f("136033.229"), _f("136033.630")),
    }
    r.slap = _interleave(_f("136033.107"), _f("136033.108"))
    # bank 1, planes 0-4, 32 KB each (two ROMs), ROMREGION_INVERT
    pairs = (("137", "138"), ("139", "140"), ("141", "142"), ("143", "144"), ("145", "146"))
    r.pf_planes = [_inv(_f("136033." + a) + _f("136033." + b)) for a, b in pairs]
    # bank 2 (motion objects), planes 0-2 at +0x4000 (tiles 2048-4095), plane 3 empty
    r.mo_planes = [_inv(_f("136033." + n)) for n in ("149", "151", "153")]
    r.prom1 = _f("136033.118")
    r.prom2 = _f("136033.119")
    return r


def _rom(r, addr, n):
    for base, d in r.prog.items():
        if base <= addr < base + len(d):
            return d[addr - base:addr - base + n]
    raise ValueError(f"address {addr:06x} outside program ROM")


def u16(r, addr):
    return struct.unpack(">H", _rom(r, addr, 2))[0]


def u32(r, addr):
    return struct.unpack(">I", _rom(r, addr, 4))[0]


def _slap8(r, bank, addr):
    # 0x80000-0x87fff: one 8 KB bank, mirrored
    return r.slap[bank * 0x2000 + (addr & 0x1FFF)]


def _slap16(r, bank, addr):
    return _slap8(r, bank, addr) << 8 | _slap8(r, bank, addr + 1)


def _slap32(r, bank, addr):
    return _slap16(r, bank, addr) << 16 | _slap16(r, bank, addr + 2)


def pf_tile(r, code):
    """Playfield tile 0..4095 -> 64 colour indices 0..31 (row-major)."""
    px = bytearray(64)
    for p, plane in enumerate(r.pf_planes):
        for row in range(8):
            v = plane[code * 8 + row]
            for c in range(8):
                if v & (0x80 >> c):
                    px[row * 8 + c] |= 1 << p
    return bytes(px)


MO_BANK = 2           # graphics bank of the motion objects
MO_FIRST_TILE = 2048  # first tile of bank 2 in ROM (149/151/153 load at plane offset 0x4000)


def mo_lookup(r, code):
    """Motion object code -> (graphics tile, palette set 0..7), atarisy1_v.cpp decode_gfx:
    lookup PROM entry 256 + (code >> 8); tile = PROM1 offset << 8 | code low byte; set from
    PROM2 (negative logic). Only bank 2 at 4 bpp is decoded."""
    e = 256 + ((code >> 8) & 0xFF)
    p1, p2 = r.prom1[e], r.prom2[e]
    if p1 & 0x20 or not p1 & 0x10 or p2 & 0x30:  # PROM1 bank 2 select active low, 4 bpp
        raise ValueError(f"motion object code {code:04x}: not bank 2, 4 bpp")
    return (p1 & 0x0F) << 8 | (code & 0xFF), ~p2 & 0x07


def mo_tile(r, tile):
    """Motion object tile (bank 2) -> 64 pens 0..7 (row-major, 0 transparent)."""
    px = bytearray(64)
    for p, plane in enumerate(r.mo_planes):
        for row in range(8):
            v = plane[(tile - MO_FIRST_TILE) * 8 + row]
            for c in range(8):
                if v & (0x80 >> c):
                    px[row * 8 + c] |= 1 << p
    return bytes(px)


def pf_lookup(r, word):
    """Playfield RAM word -> (tile code, colour set 0..3, hflip). Tile bank 0 (atarisy1_v.cpp)."""
    idx = (word >> 8) & 0x7F
    code = ((r.prom1[idx] & 0x0F) << 8) | (word & 0xFF)
    colour_set = (~r.prom2[idx] & 0x0F) >> 1  # 5 bpp: shift bpp - 4
    return code, colour_set, bool(word & 0x8000)


def _decode_row(r, bank, src, attr):
    """68010 0x1a668: 36 playfield words from a bitstream (slapstic) and attribute runs (ROM)."""
    t9 = [u16(r, CODE_TABLE + 2 * i) for i in range(32)]
    trun = [u16(r, RUN_TABLE + 2 * i) for i in range(8)]
    out = []
    st = {"cnt": 0, "hi": 0, "attr": attr}

    def next_attr():
        if st["cnt"] == 0:
            st["cnt"] = _rom(r, st["attr"], 1)[0]
            st["hi"] = _rom(r, st["attr"] + 1, 1)[0] << 8
            st["attr"] += 2
        st["cnt"] = (st["cnt"] - 1) & 0xFF
        return st["hi"]

    last = 0
    bit = 0
    while len(out) < COLS:
        v = (_slap32(r, bank, src) >> (0x12 - bit)) & 0x3FFF
        if v & 0x2000:  # 14 bit literal, bit 0 = becomes run base
            v &= 0x1FFF
            hi = next_attr()
            if v & 1:
                last = v >> 1
            out.append(((v >> 1) + hi) & 0xFFFF)
            bit += 14
        else:
            sel, n = v & 0x1C00, (v >> 7) & 7
            if sel == 0:  # run: last + 1, last + 2, ...
                for _ in range(n + 1):
                    hi = next_attr()
                    last = (last + 1) & 0xFFFF
                    out.append((last + hi) & 0xFFFF)
                bit += 7
            elif sel == 0x1C00:  # 9 bit table code
                out.append((t9[(v >> 5) & 0x1F] + next_attr()) & 0xFFFF)
                bit += 9
            elif sel <= 0x1000:  # run of one table value
                t = trun[(v >> 10) & 7]
                for _ in range(n + 1):
                    out.append((t + next_attr()) & 0xFFFF)
                bit += 7
            else:  # alternating 0x4d / 0x4e
                t = 0x4D if v & 0x400 else 0x4E
                for _ in range(n + 1):
                    out.append((t + next_attr()) & 0xFFFF)
                    t ^= 3
                bit += 7
        if bit & 0x10:
            bit &= ~0x10
            src += 2
    return out[:COLS]


def decode_level(r, n):
    """Level n (0..5) -> rows x 36 playfield words as in playfield RAM.

    Same per-row inputs as level init 0x16f6c and scroll redraw 0x13ee6: tile offset
    word and attribute byte, both indexed by row (verified against MAME, tools/pf_check.py).
    """
    d = u32(r, LEVEL_TABLE + 4 * n)
    bank = BANK[n]
    offsets = u32(r, d + 0x04)
    attr_index = u32(r, d + 0x2A)
    rows = (u16(r, d + 0x12) >> 3) + SCREEN_ROWS  # camera range + one screen
    m = []
    for row in range(rows):
        off = _slap16(r, bank, offsets + 2 * row)
        off -= 0x10000 if off & 0x8000 else 0
        attr = _slap8(r, bank, attr_index + row) + ATTR_RUNS
        m.append(_decode_row(r, bank, ROW_STREAM + off, attr))
    return m


def _irgb(raw):
    """MAME IRGB_4444: 4 bit intensity scales 4 bit R, G, B."""
    i = (raw >> 12) & 15
    i |= i << 4
    c = lambda v: (i * ((v & 15) | (v & 15) << 4)) >> 8
    return c(raw >> 8), c(raw >> 4), c(raw)


def palette(r, n):
    """1024 RGB entries after the game loads palette set n (68010 0x26b2a)."""
    pal = [0] * 1024
    for i in range(32):
        pal[i] = u16(r, PAL_ALPHA + 2 * i)
    for i in range(0xC3):
        pal[(u32(r, PAL_TARGETS + 4 * i) - 0xB00000) // 2] = u16(r, PAL_SETS + n * 0xC3 * 2 + 2 * i)
    return [_irgb(p) for p in pal]


PF_PALETTE = 0x200  # playfield colours: 0x200 + colour_set * 32 + pixel
