#!/usr/bin/env python3
"""Bubble Bobble ROM converter for galagino (MAME set boblbobl, parent bublbobl)."""

import hashlib
import io
import os
import sys
import wave
import zipfile

import numpy as np

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join("..", "pyconv"))
from asset_emit import emit_compressed, emit_plain
from convutil import fatal
from gfxutil import load_file, mame_decode
from adpcm import ima_encode
from sound_map import MUSIC, STOP, EFFECTS

ROMS = os.path.normpath(os.path.join("..", "..", "..", "romszip"))
BOOTLEG = os.path.join(ROMS, "boblbobl.zip")
PARENT = os.path.join(ROMS, "bublbobl.zip")
OUT_DIR = os.path.normpath(os.path.join("..", "..", "..", "source", "src", "machines", "bublbobl"))

SID_FILE = os.path.join(ROMS, "Bubble_Bobble.sid")
SID_SHA1 = "d9af85791a8cdd742ab1c4d567b54ac0e8d7b2b7"
AUDIO_ZIP = os.path.normpath(os.path.join("..", "..", "..", "samples", "bublbobl_samples.zip"))
AUDIO_RATE = 24000
NO_MUSIC = 0x00
STOP_MUSIC = 0xff
NO_EFFECT = 0xff

ARENA_CAPACITY = 182000
SID_RAM = 0x10000 - 0xEA40  # bublbobl.h TUNE_RAM_SIZE

GFX_LO = [("a78-09.12", "2297af6c53d5807bf90a8e081075b8c72a994fc5"),
          ("a78-10.13", "fd358c3c3b424bca285f67a1589eb98a345ff670"),
          ("a78-11.14", "33c1687ee575d66bf0e98add45d06da827813765"),
          ("a78-12.15", "0c12077d3ddc2ce6aa45a0224ad5540f3f218446"),
          ("a78-13.16", "c5a89f4d73acc0db86654540b3abfd77b3757db5"),
          ("a78-14.17", "1307b26d80e6f36ebe6c442bebec41d20066eaf9")]
GFX_HI = [("a78-15.30", "44eddf12fb46fceca2addbe6da929aaea7636b13"),
          ("a78-16.31", "d5b045e3ebaa44809757a4220cefb3c6815470da"),
          ("a78-17.32", "3326fef4e0bd86681a3047dc11886bb171ecb609"),
          ("a78-18.33", "32dce8d311a4be003693182a999e4053baa6bb0a"),
          ("a78-19.34", "b94e62b6fbe7f4e08086d0365afc5cff6e0ccafd"),
          ("a78-20.35", "29f91b5a3765e4d6e6c3382db1d8d8297b6e56c8")]
TILE_COUNT = 12288  # codes 12288..16383 decode from empty ROM space: transparent


def bootleg(name, sha1):
    return load_file(BOOTLEG, [name], sha1)


def parent(name, sha1):
    return load_file(PARENT, [name], sha1)


# fixed 32 KB ($0000-$7FFF) and 16 KB banks 0..3 ($8000-$BFFF; bb5 = banks
# 0,1, bb4 = banks 2,3). Banks 4..7 are empty in MAME and read 0x00
# (bublbobl.cpp rdMain). Two assets: 96 KB does not fit one Arena block.
def main_rom():
    bb3 = bootleg("bb3", "a48489a13bfd01949e7fd273029d9cb8bfd7be48")
    bb5 = bootleg("bb5", "5a5da40c2cc82420f70bc58ffa32de1088c6c82f")
    bb4 = bootleg("bb4", "304324074ae726501bbb08e683850639d69939fb")
    return bytes(bb3), bytes(bb5 + bb4)


def gfx_region():
    region = bytearray(0x80000)
    for i, (name, sha1) in enumerate(GFX_LO):
        region[i * 0x8000:(i + 1) * 0x8000] = parent(name, sha1)
    for i, (name, sha1) in enumerate(GFX_HI):
        region[0x40000 + i * 0x8000:0x40000 + (i + 1) * 0x8000] = parent(name, sha1)
    return bytes(b ^ 0xff for b in region)  # ROMREGION_INVERT


def decode_tiles(region):
    half = 0x40000 * 8
    planes = [0, 4, half, half + 4]  # first = MSB
    xoffs = [3, 2, 1, 0, 11, 10, 9, 8]
    yoffs = [16 * y for y in range(8)]
    return mame_decode(region, 8, 8, planes, xoffs, yoffs, 128, TILE_COUNT)


# one word per tile row, pixel x in bits 4x..4x+3
def pack_tile(tile):
    rows = []
    for row in tile:
        v = 0
        for x, pen in enumerate(row):
            v |= pen << (4 * x)
        rows.append(v)
    return tuple(rows)


TRANSPARENT = tuple([0xffffffff] * 8)


# unique tiles, index 0 = fully transparent; remap code -> unique index
def dedupe(tiles):
    unique = [TRANSPARENT]
    index = {TRANSPARENT: 0}
    remap = []
    for t in tiles:
        packed = pack_tile(t)
        if packed not in index:
            index[packed] = len(unique)
            unique.append(packed)
        remap.append(index[packed])
    return unique, remap


def body(values, fmt, per_line=16):
    rows = [",".join(fmt % v for v in values[i:i + per_line]) for i in range(0, len(values), per_line)]
    return ",\n".join(rows)


def sid_file():
    if not os.path.exists(SID_FILE):
        fatal(f"missing {os.path.abspath(SID_FILE)} (HVSC Clarke_Peter/Bubble_Bobble.sid)")
    data = open(SID_FILE, "rb").read()
    if hashlib.sha1(data).hexdigest() != SID_SHA1:
        fatal(f"{SID_FILE}: SHA1 mismatch, expected {SID_SHA1}")
    return data


def effect_pcm16(cmd):
    name = f"cmd_{cmd:02x}.wav"
    with zipfile.ZipFile(AUDIO_ZIP) as z:
        if name not in z.namelist():
            fatal(f"{name} missing in {os.path.abspath(AUDIO_ZIP)} (capture_sound.lua, split_sound.py)")
        with wave.open(io.BytesIO(z.read(name))) as w:
            if w.getframerate() != AUDIO_RATE or w.getnchannels() != 1 or w.getsampwidth() != 2:
                fatal(f"{name}: expected {AUDIO_RATE} Hz mono 16 bit")
            return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64)


# int8 PCM, one common scale for all effects: keeps the arcade's levels
def effects_pcm8():
    pcm = {cmd: effect_pcm16(cmd) for cmd in sorted(EFFECTS)}
    peak = max(np.abs(p).max() for p in pcm.values())
    return {cmd: np.clip(np.round(p * (127.0 / peak)), -128, 127).astype(np.int8) for cmd, p in pcm.items()}


def write_sound(f):
    music = [NO_MUSIC] * 256
    effect = [NO_EFFECT] * 256
    for cmd, subtune in MUSIC.items():
        music[cmd] = subtune
    for cmd in STOP:
        music[cmd] = STOP_MUSIC
    for index, cmd in enumerate(sorted(EFFECTS)):
        effect[cmd] = index
    f.write("// sound latch command -> SID subtune (1..12), 0xff stop music, 0 none\n")
    f.write(f"static const uint8_t bublbobl_music[256] = {{\n{body(music, '0x%02X')}\n}};\n")
    f.write("// sound latch command -> effect index (bublbobl_fx_blobs), 0xff none\n")
    f.write(f"static const uint8_t bublbobl_effect[256] = {{\n{body(effect, '0x%02X')}\n}};\n")


def write_samples(f, pcm8):
    total = 0
    for cmd in sorted(EFFECTS):
        enc = ima_encode(pcm8[cmd])
        total += len(enc)
        f.write(f"// cmd {cmd:02x} {EFFECTS[cmd]}: {len(pcm8[cmd]) / AUDIO_RATE:.2f} s, IMA-ADPCM: 2-byte LE\n")
        f.write("// predictor, then one nibble per sample (count = decoded samples)\n")
        emit_plain(f, f"bublbobl_fx_{EFFECTS[cmd]}", "unsigned char", "", len(pcm8[cmd]), body(list(enc), "0x%02X"))
    f.write(f"#define BUBLBOBL_EFFECT_COUNT {len(EFFECTS)}\n")
    f.write("static const FlashAsset<unsigned char, PLAIN> *const bublbobl_fx_blobs[] = {\n")
    for cmd in sorted(EFFECTS):
        f.write(f"  &bublbobl_fx_{EFFECTS[cmd]}_blob,\n")
    f.write("};\n")
    return total


def write(name, fn):
    with open(os.path.join(OUT_DIR, name), "w", newline="\n") as f:
        f.write("// Generated by romconv/internal/bublbobl/bublbobl_rom_convert.py; do not edit.\n")
        fn(f)


def main():
    main, banks = main_rom()
    sub = parent("a78-08.37", "af7a335c8da637103103cc274e077f123908ebb7")
    prom = parent("a71-25.41", "089c31e2f614145ef2743164f7b52ae35bc06808")

    write("bublbobl_main_rom.h", lambda f: emit_compressed(
        f, "bublbobl_main_rom", "unsigned char", "", len(main), list(main)))
    write("bublbobl_bank_rom.h", lambda f: emit_compressed(
        f, "bublbobl_bank_rom", "unsigned char", "", len(banks), list(banks)))
    write("bublbobl_sub_rom.h", lambda f: emit_compressed(
        f, "bublbobl_sub_rom", "unsigned char", "", len(sub), list(sub)))

    unique, remap = dedupe(decode_tiles(gfx_region()))
    # PLAIN: 237 KB, larger than the Arena; render reads a few tiles per strip
    write("bublbobl_tiles.h", lambda f: emit_plain(
        f, "bublbobl_tiles", "uint32_t", "[8]", len(unique),
        "\n".join("{" + ",".join("0x%08X" % v for v in t) + "}," for t in unique)))
    # PLAIN: read once per drawn tile, same access pattern as the tiles
    write("bublbobl_tile_map.h", lambda f: emit_plain(
        f, "bublbobl_tile_map", "unsigned short", "", len(remap), body(remap, "%d")))
    # PLAIN: 128 bytes, not worth a zlib stream and an Arena allocation
    types = list(prom[0x80:0x100])
    write("bublbobl_prom.h", lambda f: emit_plain(
        f, "bublbobl_prom", "unsigned char", "", len(types), body(types, "0x%02X")))

    # unpack order (bublbobl.cpp ctor), first fit into 2 x 91000:
    # bank -> A, main -> B, sub -> B, sid_ram -> A
    sid = sid_file()
    # PLAIN: SidPlayer copies the payload into its Arena RAM window
    write("bublbobl_sid.h", lambda f: emit_plain(
        f, "bublbobl_sid", "unsigned char", "", len(sid), body(list(sid), "0x%02X")))
    write("bublbobl_sound_map.h", write_sound)
    # PLAIN: streamed per sample on the audio core, never unpacked
    pcm8 = effects_pcm8()
    encoded = []
    write("bublbobl_samples.h", lambda f: encoded.append(write_samples(f, pcm8)))
    print(f"  flash effects {len(EFFECTS)}: {encoded[0]} bytes, sid {len(sid)}")

    arena = {"bank_rom": len(banks), "main_rom": len(main), "sub_rom": len(sub), "sid_ram": SID_RAM}
    total = sum(arena.values())
    for name, size in arena.items():
        print(f"  arena {name:9s} {size:6d}")
    print(f"  arena total     {total:6d} of {ARENA_CAPACITY}")
    print(f"  flash tiles {len(unique)} x 32 = {len(unique) * 32}, map {len(remap) * 2}")
    if total >= ARENA_CAPACITY:
        fatal("bublbobl assets exceed Arena::CAPACITY")


if __name__ == "__main__":
    main()
