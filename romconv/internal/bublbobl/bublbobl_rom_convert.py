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
from sound_map import MUSIC, STOP, EFFECTS, STAND_INS

ROMS = os.path.normpath(os.path.join("..", "..", "..", "romszip"))
BOOTLEG = os.path.join(ROMS, "boblbobl.zip")
PARENT = os.path.join(ROMS, "bublbobl.zip")
OUT_DIR = os.path.normpath(os.path.join("..", "..", "..", "source", "src", "machines", "bublbobl"))

SID_FILE = os.path.join(ROMS, "Bubble_Bobble.sid")
SID_SHA1 = "d9af85791a8cdd742ab1c4d567b54ac0e8d7b2b7"
AUDIO_ZIP = os.path.normpath(os.path.join("..", "..", "..", "samples", "bublbobl_samples.zip"))
AUDIO_RATE = 24000
FX_RATE = AUDIO_RATE // 2  # bublbobl_sound.h FxMixer interpolates back to 24 kHz
FX_CUTOFF = 5500           # Hz, anti-alias low-pass below the 6 kHz Nyquist
FX_TAPS = 63
NO_MUSIC = 0x00
STOP_MUSIC = 0xff
NO_EFFECT = 0xff

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

# tile_map entry: unique tile index, plus the flips that turn it into the code's tile
TILE_INDEX = 0x1fff
TILE_FLIPX = 0x2000
TILE_FLIPY = 0x4000


def flip_row(v):
    return sum(((v >> (4 * x)) & 0xf) << (4 * (7 - x)) for x in range(8))


def flip_tile(rows, fx, fy):
    if fy:
        rows = rows[::-1]
    if fx:
        rows = tuple(flip_row(v) for v in rows)
    return rows


# unique tiles, index 0 = fully transparent; flipped copies share one tile;
# remap code -> index | flip bits
def dedupe(tiles):
    unique = [TRANSPARENT]
    index = {TRANSPARENT: 0}
    remap = []
    for t in tiles:
        packed = pack_tile(t)
        entry = None
        for fx, fy in ((0, 0), (1, 0), (0, 1), (1, 1)):
            flipped = flip_tile(packed, fx, fy)
            if flipped in index:
                entry = index[flipped] | (TILE_FLIPX if fx else 0) | (TILE_FLIPY if fy else 0)
                break
        if entry is None:
            entry = len(unique)
            index[packed] = entry
            unique.append(packed)
        remap.append(entry)
    if len(unique) > TILE_INDEX + 1:
        fatal(f"{len(unique)} unique tiles exceed the tile_map index field")
    return unique, remap


def write_tile_map(f, remap):
    f.write(f"#define BUBLBOBL_TILE_INDEX 0x{TILE_INDEX:04X}\n")
    f.write(f"#define BUBLBOBL_TILE_FLIPX 0x{TILE_FLIPX:04X}\n")
    f.write(f"#define BUBLBOBL_TILE_FLIPY 0x{TILE_FLIPY:04X}\n")
    emit_compressed(f, "bublbobl_tile_map", "unsigned short", "", len(remap), remap)


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


# AUDIO_RATE -> FX_RATE: windowed-sinc low-pass, then every 2nd sample
def decimate(pcm):
    n = np.arange(FX_TAPS) - (FX_TAPS - 1) / 2
    fc = FX_CUTOFF / AUDIO_RATE
    h = 2 * fc * np.sinc(2 * fc * n) * np.blackman(FX_TAPS)
    return np.convolve(pcm, h / h.sum(), mode="same")[::AUDIO_RATE // FX_RATE]


# int8 PCM at FX_RATE, one common scale for all effects: keeps the arcade's
# levels. Silence at both ends dropped: saves flash, starts the effect
# ~30 ms sooner
def effects_pcm8():
    pcm = {cmd: decimate(effect_pcm16(cmd)) for cmd in sorted(EFFECTS)}
    peak = max(np.abs(p).max() for p in pcm.values())
    return {cmd: np.trim_zeros(np.clip(np.round(p * (127.0 / peak)), -128, 127).astype(np.int8))
            for cmd, p in pcm.items()}


def write_sound(f):
    music = [NO_MUSIC] * 256
    effect = [NO_EFFECT] * 256
    for cmd, subtune in MUSIC.items():
        music[cmd] = subtune
    for cmd in STOP:
        music[cmd] = STOP_MUSIC
    for index, cmd in enumerate(sorted(EFFECTS)):
        effect[cmd] = index
    for cmd, sample in STAND_INS.items():
        if cmd in EFFECTS or sample not in EFFECTS:
            fatal(f"stand-in {cmd:02x} -> {sample:02x}: needs a command without sample and an EFFECTS sample")
        effect[cmd] = effect[sample]
    f.write("// sound latch command -> SID subtune (1..12), 0xff stop music, 0 none\n")
    f.write(f"static const uint8_t bublbobl_music[256] = {{\n{body(music, '0x%02X')}\n}};\n")
    f.write("// sound latch command -> effect index (bublbobl_fx_blobs), 0xff none\n")
    f.write(f"static const uint8_t bublbobl_effect[256] = {{\n{body(effect, '0x%02X')}\n}};\n")


# Effects unpacked to the Arena (block B after the ROMs and tune RAM, ~2 KB
# spare); the rest stay PLAIN. Picked by flash saved per Arena byte.
PACKED_EFFECTS = {"fx0b", "fx25", "fx1e", "coin"}


def write_samples(f, pcm8):
    total = 0
    for cmd in sorted(EFFECTS):
        name = EFFECTS[cmd]
        enc = ima_encode(pcm8[cmd])
        total += len(enc)
        f.write(f"// cmd {cmd:02x} {name}: {len(pcm8[cmd]) / FX_RATE:.2f} s, {FX_RATE} Hz IMA-ADPCM: 2-byte LE\n")
        f.write("// predictor, then one nibble per sample\n")
        if name in PACKED_EFFECTS:
            emit_compressed(f, f"bublbobl_fx_{name}", "unsigned char", "", len(enc), list(enc),
                            plan=False)  # BublboblFxBlob holds typed pointers
        else:
            emit_plain(f, f"bublbobl_fx_{name}", "unsigned char", "", len(enc), body(list(enc), "0x%02X"),
                       plan=False)
    f.write(f"#define BUBLBOBL_EFFECT_COUNT {len(EFFECTS)}\n")
    # bublbobl.cpp checks ARENA_EXTRA holds one decode buffer per packed effect
    f.write(f"#define BUBLBOBL_PACKED_EFFECT_COUNT {sum(n in PACKED_EFFECTS for n in EFFECTS.values())}\n")
    f.write("// effect index -> ADPCM stream in flash (plain) or packed for the Arena\n")
    f.write("struct BublboblFxBlob {\n")
    f.write("  const FlashAsset<unsigned char, PLAIN> *plain;       // nullptr: packed\n")
    f.write("  const FlashAsset<unsigned char, COMPRESSED> *packed; // nullptr: plain\n")
    f.write("  uint32_t samples;                                    // decoded samples\n")
    f.write("};\n")
    f.write("static const BublboblFxBlob bublbobl_fx_blobs[] = {\n")
    for cmd in sorted(EFFECTS):
        blob = f"&bublbobl_fx_{EFFECTS[cmd]}_blob"
        plain, packed = ("nullptr", blob) if EFFECTS[cmd] in PACKED_EFFECTS else (blob, "nullptr")
        f.write(f"  {{ {plain}, {packed}, {len(pcm8[cmd])} }},\n")
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
    write("bublbobl_tile_map.h", lambda f: write_tile_map(f, remap))
    # PLAIN: 128 bytes, not worth a zlib stream and an Arena allocation
    types = list(prom[0x80:0x100])
    write("bublbobl_prom.h", lambda f: emit_plain(
        f, "bublbobl_prom", "unsigned char", "", len(types), body(types, "0x%02X")))

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

    print(f"  flash tiles {len(unique)} x 32 = {len(unique) * 32}")


if __name__ == "__main__":
    main()
