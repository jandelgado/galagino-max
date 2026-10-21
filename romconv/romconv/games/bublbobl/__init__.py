"""Bubble Bobble ROM converter for galagino (MAME set boblbobl, parent bublbobl)."""
import io
import wave
import zipfile

import numpy as np

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed, emit_plain
from romconv.emit.write import banner
from romconv.games.bublbobl.sound_map import EFFECTS, MUSIC, STAND_INS, STOP
from romconv.gfx.adpcm import ima_encode
from romconv.gfx.gfxutil import mame_decode
from romconv.misc.userfile import read_user_file

_BOOTLEG = "boblbobl.zip"
_PARENT = "bublbobl.zip"

_SID_FILE = "Bubble_Bobble.sid"
_SID_SHA1 = "d9af85791a8cdd742ab1c4d567b54ac0e8d7b2b7"
_AUDIO_ZIP = "bublbobl_samples.zip"  # samples/
_AUDIO_RATE = 24000
_FX_RATE = _AUDIO_RATE // 2  # bublbobl_sound.h FxMixer interpolates back to 24 kHz
_FX_CUTOFF = 5500            # Hz, anti-alias low-pass below the 6 kHz Nyquist
_FX_TAPS = 63
_NO_MUSIC = 0x00
_STOP_MUSIC = 0xff
_NO_EFFECT = 0xff

_GFX_LO = (("a78-09.12", "2297af6c53d5807bf90a8e081075b8c72a994fc5"),
           ("a78-10.13", "fd358c3c3b424bca285f67a1589eb98a345ff670"),
           ("a78-11.14", "33c1687ee575d66bf0e98add45d06da827813765"),
           ("a78-12.15", "0c12077d3ddc2ce6aa45a0224ad5540f3f218446"),
           ("a78-13.16", "c5a89f4d73acc0db86654540b3abfd77b3757db5"),
           ("a78-14.17", "1307b26d80e6f36ebe6c442bebec41d20066eaf9"))
_GFX_HI = (("a78-15.30", "44eddf12fb46fceca2addbe6da929aaea7636b13"),
           ("a78-16.31", "d5b045e3ebaa44809757a4220cefb3c6815470da"),
           ("a78-17.32", "3326fef4e0bd86681a3047dc11886bb171ecb609"),
           ("a78-18.33", "32dce8d311a4be003693182a999e4053baa6bb0a"),
           ("a78-19.34", "b94e62b6fbe7f4e08086d0365afc5cff6e0ccafd"),
           ("a78-20.35", "29f91b5a3765e4d6e6c3382db1d8d8297b6e56c8"))
_TILE_COUNT = 12288  # codes 12288..16383 decode from empty ROM space: transparent


def _bootleg(ctx, name, sha1):
    return ctx.rom(name, sha1, zip=_BOOTLEG)


def _parent(ctx, name, sha1):
    return ctx.rom(name, sha1, zip=_PARENT)


# fixed 32 KB ($0000-$7FFF) and 16 KB banks 0..3 ($8000-$BFFF; bb5 = banks
# 0,1, bb4 = banks 2,3). Banks 4..7 are empty in MAME and read 0x00
# (bublbobl.cpp rdMain). Two assets: 96 KB does not fit one Arena block.
def _main_rom(ctx):
    bb3 = _bootleg(ctx, "bb3", "a48489a13bfd01949e7fd273029d9cb8bfd7be48")
    bb5 = _bootleg(ctx, "bb5", "5a5da40c2cc82420f70bc58ffa32de1088c6c82f")
    bb4 = _bootleg(ctx, "bb4", "304324074ae726501bbb08e683850639d69939fb")
    return bytes(bb3), bytes(bb5 + bb4)


def _gfx_region(ctx):
    region = bytearray(0x80000)
    for i, (name, sha1) in enumerate(_GFX_LO):
        region[i * 0x8000:(i + 1) * 0x8000] = _parent(ctx, name, sha1)
    for i, (name, sha1) in enumerate(_GFX_HI):
        region[0x40000 + i * 0x8000:0x40000 + (i + 1) * 0x8000] = _parent(ctx, name, sha1)
    return bytes(b ^ 0xff for b in region)  # ROMREGION_INVERT


def _decode_tiles(region):
    half = 0x40000 * 8
    planes = [0, 4, half, half + 4]  # first = MSB
    xoffs = [3, 2, 1, 0, 11, 10, 9, 8]
    yoffs = [16 * y for y in range(8)]
    return mame_decode(region, 8, 8, planes, xoffs, yoffs, 128, _TILE_COUNT)


# one word per tile row, pixel x in bits 4x..4x+3
def _pack_tile(tile):
    rows = []
    for row in tile:
        v = 0
        for x, pen in enumerate(row):
            v |= pen << (4 * x)
        rows.append(v)
    return tuple(rows)


_TRANSPARENT = tuple([0xffffffff] * 8)

# tile_map entry: unique tile index, plus the flips that turn it into the code's tile
_TILE_INDEX = 0x1fff
_TILE_FLIPX = 0x2000
_TILE_FLIPY = 0x4000


def _flip_row(v):
    return sum(((v >> (4 * x)) & 0xf) << (4 * (7 - x)) for x in range(8))


def _flip_tile(rows, fx, fy):
    if fy:
        rows = rows[::-1]
    if fx:
        rows = tuple(_flip_row(v) for v in rows)
    return rows


# unique tiles, index 0 = fully transparent; flipped copies share one tile;
# remap code -> index | flip bits
def _dedupe(tiles):
    unique = [_TRANSPARENT]
    index = {_TRANSPARENT: 0}
    remap = []
    for t in tiles:
        packed = _pack_tile(t)
        entry = None
        for fx, fy in ((0, 0), (1, 0), (0, 1), (1, 1)):
            flipped = _flip_tile(packed, fx, fy)
            if flipped in index:
                entry = index[flipped] | (_TILE_FLIPX if fx else 0) | (_TILE_FLIPY if fy else 0)
                break
        if entry is None:
            entry = len(unique)
            index[packed] = entry
            unique.append(packed)
        remap.append(entry)
    if len(unique) > _TILE_INDEX + 1:
        raise ConversionError(f"{len(unique)} unique tiles exceed the tile_map index field")
    return unique, remap


def _write_tile_map(f, remap):
    f.write(f"#define BUBLBOBL_TILE_INDEX 0x{_TILE_INDEX:04X}\n")
    f.write(f"#define BUBLBOBL_TILE_FLIPX 0x{_TILE_FLIPX:04X}\n")
    f.write(f"#define BUBLBOBL_TILE_FLIPY 0x{_TILE_FLIPY:04X}\n")
    emit_compressed(f, "bublbobl_tile_map", "unsigned short", "", len(remap), remap)


def _body(values, fmt, per_line=16):
    rows = [",".join(fmt % v for v in values[i:i + per_line]) for i in range(0, len(values), per_line)]
    return ",\n".join(rows)


def _effect_pcm16(ctx, cmd):
    name = f"cmd_{cmd:02x}.wav"
    path = ctx.paths.samples / _AUDIO_ZIP
    if not path.is_file():
        raise ConversionError(f"{name}: {path} not found (capture_sound.lua, split_sound.py)")
    with zipfile.ZipFile(path) as z:
        if name not in z.namelist():
            raise ConversionError(f"{name} missing in {path} (capture_sound.lua, split_sound.py)")
        with wave.open(io.BytesIO(z.read(name))) as w:
            if w.getframerate() != _AUDIO_RATE or w.getnchannels() != 1 or w.getsampwidth() != 2:
                raise ConversionError(f"{name}: expected {_AUDIO_RATE} Hz mono 16 bit")
            return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64)


# AUDIO_RATE -> FX_RATE: windowed-sinc low-pass, then every 2nd sample
def _decimate(pcm):
    n = np.arange(_FX_TAPS) - (_FX_TAPS - 1) / 2
    fc = _FX_CUTOFF / _AUDIO_RATE
    h = 2 * fc * np.sinc(2 * fc * n) * np.blackman(_FX_TAPS)
    return np.convolve(pcm, h / h.sum(), mode="same")[::_AUDIO_RATE // _FX_RATE]


# int8 PCM at FX_RATE, one common scale for all effects: keeps the arcade's
# levels. Silence at both ends dropped: saves flash, starts the effect
# ~30 ms sooner
def _effects_pcm8(ctx):
    pcm = {cmd: _decimate(_effect_pcm16(ctx, cmd)) for cmd in sorted(EFFECTS)}
    peak = max(np.abs(p).max() for p in pcm.values())
    return {cmd: np.trim_zeros(np.clip(np.round(p * (127.0 / peak)), -128, 127).astype(np.int8))
            for cmd, p in pcm.items()}


def _write_sound(f):
    music = [_NO_MUSIC] * 256
    effect = [_NO_EFFECT] * 256
    for cmd, subtune in MUSIC.items():
        music[cmd] = subtune
    for cmd in STOP:
        music[cmd] = _STOP_MUSIC
    for index, cmd in enumerate(sorted(EFFECTS)):
        effect[cmd] = index
    for cmd, sample in STAND_INS.items():
        if cmd in EFFECTS or sample not in EFFECTS:
            raise ConversionError(
                f"stand-in {cmd:02x} -> {sample:02x}: needs a command without sample and an EFFECTS sample")
        effect[cmd] = effect[sample]
    f.write("// sound latch command -> SID subtune (1..12), 0xff stop music, 0 none\n")
    f.write(f"static const uint8_t bublbobl_music[256] = {{\n{_body(music, '0x%02X')}\n}};\n")
    f.write("// sound latch command -> effect index (bublbobl_fx_blobs), 0xff none\n")
    f.write(f"static const uint8_t bublbobl_effect[256] = {{\n{_body(effect, '0x%02X')}\n}};\n")


# Effects unpacked to the Arena (block B after the ROMs and tune RAM, ~2 KB
# spare); the rest stay PLAIN. Picked by flash saved per Arena byte.
_PACKED_EFFECTS = frozenset({"fx0b", "fx25", "fx1e", "coin"})


def _write_samples(f, pcm8):
    total = 0
    for cmd in sorted(EFFECTS):
        name = EFFECTS[cmd]
        enc = ima_encode(pcm8[cmd])
        total += len(enc)
        f.write(f"// cmd {cmd:02x} {name}: {len(pcm8[cmd]) / _FX_RATE:.2f} s, {_FX_RATE} Hz IMA-ADPCM: 2-byte LE\n")
        f.write("// predictor, then one nibble per sample\n")
        if name in _PACKED_EFFECTS:
            emit_compressed(f, f"bublbobl_fx_{name}", "unsigned char", "", len(enc), list(enc),
                            plan=False)  # BublboblFxBlob holds typed pointers
        else:
            emit_plain(f, f"bublbobl_fx_{name}", "unsigned char", "", len(enc), _body(list(enc), "0x%02X"),
                       plan=False)
    f.write(f"#define BUBLBOBL_EFFECT_COUNT {len(EFFECTS)}\n")
    # bublbobl.cpp checks ARENA_EXTRA holds one decode buffer per packed effect
    f.write(f"#define BUBLBOBL_PACKED_EFFECT_COUNT {sum(n in _PACKED_EFFECTS for n in EFFECTS.values())}\n")
    f.write("// effect index -> ADPCM stream in flash (plain) or packed for the Arena\n")
    f.write("struct BublboblFxBlob {\n")
    f.write("  const FlashAsset<unsigned char, PLAIN> *plain;       // nullptr: packed\n")
    f.write("  const FlashAsset<unsigned char, COMPRESSED> *packed; // nullptr: plain\n")
    f.write("  uint32_t samples;                                    // decoded samples\n")
    f.write("};\n")
    f.write("static const BublboblFxBlob bublbobl_fx_blobs[] = {\n")
    for cmd in sorted(EFFECTS):
        blob = f"&bublbobl_fx_{EFFECTS[cmd]}_blob"
        plain, packed = ("nullptr", blob) if EFFECTS[cmd] in _PACKED_EFFECTS else (blob, "nullptr")
        f.write(f"  {{ {plain}, {packed}, {len(pcm8[cmd])} }},\n")
    f.write("};\n")
    return total


def _write(ctx, name, fn):
    with ctx.header(name) as f:
        f.write(banner(__file__))
        fn(f)


def _convert(ctx: Context) -> None:
    main, banks = _main_rom(ctx)
    sub = _parent(ctx, "a78-08.37", "af7a335c8da637103103cc274e077f123908ebb7")
    prom = _parent(ctx, "a71-25.41", "089c31e2f614145ef2743164f7b52ae35bc06808")

    _write(ctx, "bublbobl_main_rom.h", lambda f: emit_compressed(
        f, "bublbobl_main_rom", "unsigned char", "", len(main), list(main)))
    _write(ctx, "bublbobl_bank_rom.h", lambda f: emit_compressed(
        f, "bublbobl_bank_rom", "unsigned char", "", len(banks), list(banks)))
    _write(ctx, "bublbobl_sub_rom.h", lambda f: emit_compressed(
        f, "bublbobl_sub_rom", "unsigned char", "", len(sub), list(sub)))

    unique, remap = _dedupe(_decode_tiles(_gfx_region(ctx)))
    # PLAIN: 237 KB, larger than the Arena; render reads a few tiles per strip
    _write(ctx, "bublbobl_tiles.h", lambda f: emit_plain(
        f, "bublbobl_tiles", "uint32_t", "[8]", len(unique),
        "\n".join("{" + ",".join("0x%08X" % v for v in t) + "}," for t in unique)))
    _write(ctx, "bublbobl_tile_map.h", lambda f: _write_tile_map(f, remap))
    # PLAIN: 128 bytes, not worth a zlib stream and an Arena allocation
    types = list(prom[0x80:0x100])
    _write(ctx, "bublbobl_prom.h", lambda f: emit_plain(
        f, "bublbobl_prom", "unsigned char", "", len(types), _body(types, "0x%02X")))

    sid = read_user_file(ctx, _SID_FILE, _SID_SHA1, "HVSC Clarke_Peter/Bubble_Bobble.sid")
    # PLAIN: SidPlayer copies the payload into its Arena RAM window
    _write(ctx, "bublbobl_sid.h", lambda f: emit_plain(
        f, "bublbobl_sid", "unsigned char", "", len(sid), _body(list(sid), "0x%02X")))
    _write(ctx, "bublbobl_sound_map.h", _write_sound)
    # PLAIN: streamed per sample on the audio core, never unpacked
    pcm8 = _effects_pcm8(ctx)
    encoded = []
    _write(ctx, "bublbobl_samples.h", lambda f: encoded.append(_write_samples(f, pcm8)))
    ctx.log.info(f"  flash effects {len(EFFECTS)}: {encoded[0]} bytes, sid {len(sid)}")

    ctx.log.info(f"  flash tiles {len(unique)} x 32 = {len(unique) * 32}")


JOB = Job(name="bublbobl", convert=_convert, machine="bublbobl", zips=(_BOOTLEG, _PARENT), plan=True)
