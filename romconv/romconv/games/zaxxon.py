# ============================================================
# Zaxxon and Super Zaxxon (Sega 1982) ROM converter for GALAGINO
# Set MAME "zaxxon" (US rev D). Verified: every ROM's SHA1 matches MAME's
# zaxxon.cpp ROM_START(zaxxon) exactly (silkscreen U-numbers in our dump
# also match the board diagram in that driver's header comment).
# ============================================================
import io
import wave
from types import MappingProxyType

import numpy as np

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed, emit_plain
from romconv.gfx.adpcm import ima_encode
from romconv.gfx.color import prom_rgb_332_resnet, rgb565_swapped
from romconv.gfx.gfxutil import mame_decode, rot_galagino
from romconv.hw import segacrpt

_AUDIO_ZIP = "zaxxon-audio.zip"

# Discrete-sound sample set (Sega "with samples" board, zaxxon_a.cpp /
# zaxxon_sample_names): 12 wav files, named by MAME's sample index. Same
# source as the Battles/Xevious explosion samples (mamewavs), here the full
# board set incl. missiles/laser/alarms/engine noise.
# Stored rate per sample, from the share of energy above 4 / 6 kHz
# (COMPRESSION.md "Lower rates for the 24 kHz sets"): explosions and noise
# loops carry nothing above 4 kHz -> 8 kHz; cannon, shot, homing missile
# and laser -> 12 kHz; the alarms keep 24 kHz.
_AUDIO_SAMPLES = (
    ("00.wav", "battleship",       "Battleship (end-of-level boss), looped",  8000),
    ("01.wav", "laser",            "Laser / force field, looped",             12000),
    ("02.wav", "missile_base",     "Base missile, one-shot",                  8000),
    ("03.wav", "missile_homing",   "Homing missile, looped",                  12000),
    ("04.wav", "noise_asteroid",   "Asteroid field background noise, looped", 8000),
    ("05.wav", "noise_intro",      "Initial background noise, looped",        8000),
    ("08.wav", "cannon",           "Ship cannon fire, one-shot",              12000),
    ("10.wav", "explosion_ship",   "Player ship explosion, one-shot",         8000),
    ("11.wav", "explosion_enemy",  "Enemy explosion, one-shot",               8000),
    ("20.wav", "alarm_fuel",       "Low fuel alarm, one-shot",                24000),
    ("21.wav", "alarm_lock",       "Target lock alarm, one-shot",             24000),
    ("23.wav", "shot",             "Enemy shot, one-shot",                    12000),
)
_OUTPUT_RATE = 24000  # Audio's fixed output rate (audio.cpp)
# Audio holds each stored sample OUTPUT_RATE / rate output samples,
# linearly interpolated (zaxxon_sample_<name>_hold, Audio::zaxxon_render_buffer).

ZAXXON_FILES = MappingProxyType({
    # (name in zaxxon.zip, SHA1)
    "maincpu1": ("zaxxon3.u27", "80ac53c554c84226b119cbe3cf3470bcdbcd5762"),  # 0000-1fff
    "maincpu2": ("zaxxon2.u28", "0cd259be3fa80f3d53dfa76d5ca06773cdfe5945"),  # 2000-3fff
    "maincpu3": ("zaxxon1.u29", "2588be06ea7baca6112d58c78a1eeb98aad8a02e"),  # 4000-4fff
    "gfx_tx_1": ("zaxxon14.u68", "425157a1625b1bd5169c3218b958010bf6af12bb"),
    "gfx_tx_2": ("zaxxon15.u69", "f1ded2173eb139f48d2ca86c5ef00acbe6c11cd3"),
    "gfx_bg_1": ("zaxxon6.u113", "a002f3441b0f0044615ce71ecbd14edadba16270"),
    "gfx_bg_2": ("zaxxon5.u112", "a86543727389931244ba8a576b543d7ac05a2585"),
    "gfx_bg_3": ("zaxxon4.u111", "a8cd27dfb4a606bae8bfddcf936e69e980fb1977"),
    "gfx_spr_1": ("zaxxon11.u77", "194e2ca0a806e0cb6bb7cc8341d1fc6f2ea911f6"),
    "gfx_spr_2": ("zaxxon12.u78", "af6a5984c3cedfa8c9efcd669f4f205b51a433b2"),
    "gfx_spr_3": ("zaxxon13.u79", "4ac79cccc30e4adfa878b36101e97e20ac010438"),
    "tilemap_1": ("zaxxon8.u91", "e1f90716236c61df61bdc6915a8e390cb4dcbf15"),
    "tilemap_2": ("zaxxon7.u90", "d26a9049541479b8b19f5aa0690cf4aaa787c9b5"),
    "tilemap_3": ("zaxxon10.u93", "a0f8c15ff75affa3532abf8f340811cf415421fd"),
    "tilemap_4": ("zaxxon9.u92", "2124363be8f590b74e2b15dd3f90d77dd9ca9528"),
    # mro16.u76 in MAME; loaded first in the "proms" region -> the RGB palette.
    "prom_colors": ("zaxxon.u98", "01ae8450ccc302e1a5ae74230d44f6f531a962e2"),
    # zaxxon.u72 in MAME; loaded second -> foreground tile color-group select.
    "prom_fgsel": ("zaxxon.u72", "0cf08fb62f77d93ff7cb883c633e0db35906e11d"),
})

# Super Zaxxon (MAME ROM_START(szaxxon)): same board and layout, main CPU
# encrypted with the Sega 315-5013. Samples are zaxxon's.
_SZAXXON_FILES = MappingProxyType({
    "maincpu1": ("1804e.u27", "b5d3beb296d52ed69b4ceacf329c20a72e3a1dce"),
    "maincpu2": ("1803e.u28", "afb2bd2ffee3f5e589064f59b6ac21ed915094df"),
    "maincpu3": ("1802e.u29", "91e3a0c0df6c9cf66980d1ffcc3830ffdbef8c2f"),
    "gfx_tx_1": ("1815b.u68", "9f92bd15466048a5665bfc2ebc8c6504af9353eb"),
    "gfx_tx_2": ("1816b.u69", "42ab7dc0e4e0d09213054597373383cdb6a55699"),
    "gfx_bg_1": ("1807b.u113", "8682217dc800f43b73cd5e8501dbf3b7cd136dc1"),
    "gfx_bg_2": ("1806b.u112", "a1bee07aa906366aa69866d1bdff38e2d90fafdd"),
    "gfx_bg_3": ("1805b.u111", "f898e42d6bc1fd3629c9caee3c2af27805969ac6"),
    "gfx_spr_1": ("1812e.u77", "d4085f15fcbfb9547a7f9e2cb7ce9276c4d6c08d"),
    "gfx_spr_2": ("1813e.u78", "118e9d2b4f5daf96f5a38ccd92d0b046a470b0b2"),
    "gfx_spr_3": ("1814e.u79", "2b3305dd55dc09d7394ed8ae691773972dba28b9"),
    "tilemap_1": ("1809b.u91", "8170dd9f81c41104694951a2c74405d0c6d8b9b6"),
    "tilemap_2": ("1808b.u90", "1e4d460ce8cca66b081ee8ec1a9adb6ef98274ec"),
    "tilemap_3": ("1811b.u93", "b78c44d92078552835a20bcb7125fc9ca8af5048"),
    "tilemap_4": ("1810b.u92", "51541ec78ab3f8241a5ddf7f99a46f5e44292992"),
    "prom_colors": ("pr-5168.u98", "42840e9ab303fb64102a1dbae03d66c9cf743a9f"),
    "prom_fgsel": ("pr-5167.u72", "0cf08fb62f77d93ff7cb883c633e0db35906e11d"),
})

def _planes3(region_bits):
    return [2*(region_bits//3), 1*(region_bits//3), 0*(region_bits//3)]

def _planes2(region_bits):
    return [1*(region_bits//2), 0*(region_bits//2)]

# ------------------------------------------------------------
# Layout: gfx_8x8x2_planar (MAME standard) -- used for gfx_tx (chars)
_CHAR_XOFFS = (0, 1, 2, 3, 4, 5, 6, 7)
_CHAR_YOFFS = tuple(y * 8 for y in range(8))
_CHAR_BITS_PER_TILE = 8*8

# Layout: gfx_8x8x3_planar (MAME standard) -- used for gfx_bg (bg tiles)
_BG_XOFFS = (0, 1, 2, 3, 4, 5, 6, 7)
_BG_YOFFS = tuple(y * 8 for y in range(8))
_BG_BITS_PER_TILE = 8*8

# Layout: zaxxon_spritelayout (zaxxon.cpp, hand-verified against driver
# source), 32x32 3bpp, RGN_FRAC(1,3) planes. Sprite is a 4x4 grid of 8x8
# blocks; xoffs walks the 4 horizontal blocks, yoffs the 4 vertical ones.
_SPR_XOFFS = tuple([x for x in range(8)] +
                   [8*8 + x for x in range(8)] +
                   [16*8 + x for x in range(8)] +
                   [24*8 + x for x in range(8)])
_SPR_YOFFS = tuple([y*8 for y in range(8)] +
                   [32*8 + y*8 for y in range(8)] +
                   [64*8 + y*8 for y in range(8)] +
                   [96*8 + y*8 for y in range(8)])
_SPR_BITS_PER_TILE = 128*8

# ------------------------------------------------------------
def decode_char_tiles(gfx_tx):
    assert len(gfx_tx) == 0x1000
    tx_bits = len(gfx_tx) * 8
    char_count = tx_bits // 2 // _CHAR_BITS_PER_TILE
    return [rot_galagino(t) for t in
            mame_decode(gfx_tx, 8, 8, _planes2(tx_bits), _CHAR_XOFFS, _CHAR_YOFFS,
                        _CHAR_BITS_PER_TILE, char_count)]

def decode_bg_tiles(gfx_bg):
    assert len(gfx_bg) == 0x6000
    bg_bits = len(gfx_bg) * 8
    bg_count = bg_bits // 3 // _BG_BITS_PER_TILE
    return [rot_galagino(t) for t in
            mame_decode(gfx_bg, 8, 8, _planes3(bg_bits), _BG_XOFFS, _BG_YOFFS,
                        _BG_BITS_PER_TILE, bg_count)]

def decode_sprite_tiles(gfx_spr):
    assert len(gfx_spr) == 0x6000
    spr_bits = len(gfx_spr) * 8
    spr_count = spr_bits // 3 // _SPR_BITS_PER_TILE
    return [rot_galagino(t) for t in
            mame_decode(gfx_spr, 32, 32, _planes3(spr_bits), _SPR_XOFFS, _SPR_YOFFS,
                        _SPR_BITS_PER_TILE, spr_count)]

# ------------------------------------------------------------

# Drastically shrinks the source WAVs (44100/22050 Hz, 16 bit) down to the
# 24000 Hz signed-8bit format the other machines' samples already use (see
# romconv/games/namco/xevious.py write_sample_boom): resample to the fixed audio
# output rate (no runtime resampling needed) and requantize to 8 bit,
# peak-normalized per clip.
def _low_pass_taps(cutoff, taps=63):
    # Hamming windowed sinc, cutoff in cycles per input sample, unity DC gain
    n = np.arange(taps) - (taps - 1) / 2
    h = 2 * cutoff * np.sinc(2 * cutoff * n) * np.hamming(taps)
    return h / h.sum()

def _resample_to_8bit(wav_file, rate):
    w = wave.open(wav_file, "rb")
    nch, sw, fr, nframes = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    frames = w.readframes(nframes)
    dtype = {1: np.int8, 2: "<i2", 4: "<i4"}[sw]
    data = np.frombuffer(frames, dtype=dtype).astype(np.float64)
    if nch > 1:
        data = data.reshape(-1, nch).mean(axis=1)

    # below the output rate the stored rate's Nyquist cuts into the audio
    # band: low pass first, else higher content folds into audible
    # aliases. 24 kHz output stays byte identical.
    if rate < min(fr, _OUTPUT_RATE):
        data = np.convolve(data, _low_pass_taps(0.45 * rate / fr), mode="same")

    n_out = int(len(data) * rate / fr)
    x_old = np.linspace(0, 1, len(data), endpoint=False)
    x_new = np.linspace(0, 1, n_out, endpoint=False)
    resampled = np.interp(x_new, x_old, data)

    peak = np.abs(resampled).max()
    scale = 127.0 / peak if peak > 0 else 1.0
    return np.clip(np.round(resampled * scale), -128, 127).astype(np.int8)

def _write_samples(ctx):
    # PLAIN/flash-resident, not Asset<COMPRESSED>: the existing gfx assets
    # already fill Arena to ~17KB spare (see ARENA BUDGET note in
    # zaxxon_rom_main.h), and up to 12 of these can play concurrently, so
    # heap-decompressing even one (let alone several at once) doesn't fit --
    # same tradeoff as zaxxon_tilemap.h. Encoded as 4-bit IMA-ADPCM (see
    # romconv/romconv/gfx/adpcm.py), decoded inline per-sample by
    # Audio::zaxxon_render_buffer (ima_adpcm_decode in audio.cpp) -- halves
    # flash usage vs raw 8-bit PCM with no heap/RAM cost.
    total_pcm = total_encoded = 0
    for wav_name, suffix, comment, rate in _AUDIO_SAMPLES:
        if _OUTPUT_RATE % rate:
            raise ConversionError(f"{suffix}: rate {rate} does not divide {_OUTPUT_RATE}")
        sym = f"zaxxon_sample_{suffix}"
        samples8 = _resample_to_8bit(io.BytesIO(bytes(ctx.rom(wav_name, zip=_AUDIO_ZIP))), rate)
        encoded = ima_encode(samples8)
        flat = [int(b) for b in encoded]
        with ctx.header(f"{sym}.h") as f:
            print(f"// {comment} (zaxxon-audio.zip:{wav_name}), resampled at", file=f)
            print(f"// {rate} Hz and encoded as 4-bit IMA-ADPCM: a 2-byte", file=f)
            print("// little-endian initial predictor followed by one nibble per", file=f)
            print("// remaining sample. PLAIN/flash-resident: see ARENA BUDGET note in", file=f)
            print("// zaxxon_rom_main.h -- up to 12 of these play concurrently and Arena", file=f)
            print("// has no headroom left for heap decompression; the Asset count", file=f)
            print("// below is the decoded sample count, not the encoded byte count.", file=f)
            print(f"// {len(samples8)} samples ({len(samples8)/rate:.3f}s)", file=f)
            # never planned: count is samples, zaxxon_sound.cpp reads the PLAIN blobs
            emit_plain(f, sym, "unsigned char", "", len(samples8), _plain_hex_block(flat), plan=False)
            print(f"// output samples per stored sample ({_OUTPUT_RATE} / {rate} Hz)", file=f)
            print(f"static const uint8_t {sym}_hold = {_OUTPUT_RATE // rate};", file=f)
        total_pcm += len(samples8)
        total_encoded += len(encoded)
        ctx.log.info(f"{sym}.h: {len(samples8)} samples ({len(samples8)/rate:.3f}s @{rate}Hz) "
              f"{len(samples8)} -> {len(encoded)} bytes PLAIN")
    ctx.log.info(f"zaxxon samples total: {total_pcm} -> {total_encoded} bytes "
          f"({100.0 * (1.0 - total_encoded / total_pcm):.1f}% smaller)")

def _write_rom(ctx, name, sym, data, comment, plan=True):
  with ctx.header(name) as f:
    for line in comment.splitlines():
      print(f"// {line}" if line else "//", file=f)
    emit_plain(f, sym, "unsigned char", "", len(data), _plain_hex_block(data), plan=plan)

def _write_char_tiles(ctx, prefix, tiles):
    with ctx.header(f"{prefix}_chartiles.h") as f:
        print("// Zaxxon foreground char set (gfx_tx). 256 tile 8x8 2bpp", file=f)
        print("// (pixel values 0-3). Color group comes from the prom_fgsel", file=f)
        print(f"// lookup table ({prefix}_fgcolor_codes), not from tile data.", file=f)
        print(f"// PLAIN/flash-resident: see ARENA BUDGET note in {prefix}_rom_main.h.", file=f)
        body = ",\n".join("{" + ",".join("{" + ",".join(str(v) for v in t[y]) + "}" for y in range(8)) + "}"
                           for t in tiles)
        emit_plain(f, f"{prefix}_chartiles", "unsigned char", "[8][8]", len(tiles), body)

def _write_bg_tiles(ctx, prefix, tiles):
    with ctx.header(f"{prefix}_bgtiles.h") as f:
        print("// Zaxxon background tile graphics (gfx_bg). 1024 tile 8x8", file=f)
        print("// 3bpp (pixel values 0-7). Which tile/color goes where on the", file=f)
        print(f"// 32x512 playfield comes from {prefix}_tilemap.h, not here.", file=f)
        flat = [v for t in tiles for y in range(8) for v in t[y]]
        emit_compressed(f, f"{prefix}_bgtiles", "unsigned char", "[8][8]", len(tiles), flat)

def _write_sprite_tiles(ctx, prefix, tiles):
    with ctx.header(f"{prefix}_spritetiles.h") as f:
        print("// Zaxxon sprites (gfx_spr). 64 sprite 32x32 3bpp (pixel", file=f)
        print("// values 0-7). COMPRESSED: see ARENA BUDGET note in", file=f)
        print(f"// {prefix}_rom_main.h -- this is one of the two 65536-byte", file=f)
        print("// assets that must each anchor its own Arena block.", file=f)
        flat = [v for t in tiles for y in range(32) for v in t[y]]
        emit_compressed(f, f"{prefix}_spritetiles", "unsigned char", "[32][32]", len(tiles), flat)

def _plain_hex_block(values, per_line=32):
    hexs = ["0x{:02X}".format(v) for v in values]
    rows = [",".join(hexs[i:i + per_line]) for i in range(0, len(hexs), per_line)]
    return ",\n".join(rows)

def _write_tilemap(ctx, prefix, tile_code, tile_color):
    with ctx.header(f"{prefix}_tilemap.h") as f:
        print("// Zaxxon background playfield map: fixed 32 col x 512 row", file=f)
        print("// tilemap (16384 cells, row-major, TILEMAP_SCAN_ROWS order).", file=f)
        print(f"// tile_code indexes {prefix}_bgtiles (0-1023); tile_color is", file=f)
        print("// the 4-bit bg color group (0-15).", file=f)
        print(f"// Both COMPRESSED. See ARENA BUDGET note in {prefix}_rom_main.h.", file=f)
        emit_compressed(f, f"{prefix}_tilemap_code", "unsigned short", "", len(tile_code), list(tile_code))
        emit_compressed(f, f"{prefix}_tilemap_color", "unsigned char", "", len(tile_color), list(tile_color))

# ---- Palette PROM (mro16/u76, 256 colors) -> RGB565 byte-swapped ----
# zaxxon_state::zaxxon_palette: R = bit0-2 (1k/470/220), G = bit3-5
# (1k/470/220), B = bit6-7 (470/220). Same resistor network/weights as
# pooyan/rocnrope: compute_resistor_weights(1k/470/220) -> 33,71,151;
# (470/220) -> 81,174.
def _convert_palette(prom):
    return [rgb565_swapped(*prom_rgb_332_resnet(v)) for v in prom]  # clamped, byte-swapped for SPI

def _write_palette(ctx, prefix, rgb565, fgsel):
    with ctx.header(f"{prefix}_palette.h") as f:
        print("// Zaxxon palette: 256 entries, RGB565 byte-swapped for SPI.", file=f)
        emit_compressed(f, f"{prefix}_palette", "unsigned short", "", len(rgb565), rgb565)
        print("", file=f)
        print("// Foreground char color-group select (prom zaxxon.u72, raw).", file=f)
        print("// Indexed [sx + 32*(sy/4)] by fg tile screen column/quadrant;", file=f)
        print("// low nibble * 2 is the palette group for that character.", file=f)
        emit_compressed(f, f"{prefix}_fgcolor_codes", "unsigned char", "", len(fgsel), list(fgsel))

# ------------------------------------------------------------
def _convert_set(ctx: Context, prefix: str, files, rom_title: str, crypt=None) -> None:
  """One board set: gfx, tilemap, palette and main ROM as <prefix>_*.h.
  crypt: 315-5xxx table; then rom_main is the data image and
  <prefix>_rom_ops.h the opcode image."""
  roms = {k: ctx.rom(*v) for k, v in files.items()}

  # ---- program ROM (0000-4fff) ----
  maincpu = roms["maincpu1"] + roms["maincpu2"] + roms["maincpu3"]
  assert len(maincpu) == 0x5000
  if crypt:
    maincpu, ops = segacrpt.decrypt(maincpu, crypt)
    _write_ops(ctx, prefix, ops)
  _write_rom(ctx, f"{prefix}_rom_main.h", f"{prefix}_rom_main", maincpu,
            f"{rom_title}\n"
            "\n"
            "ARENA BUDGET (see arena.h): every COMPRESSED asset this machine\n"
            "uses stays resident for the machine's whole lifetime, so they all\n"
            "have to bin-pack into Arena's two blocks (91000, 110500) at once.\n"
            f"This ROM and {prefix}_chartiles are PLAIN (flash): they gain least\n"
            "from compression (20480 -> ~14000, 16384 -> ~1250), so the Arena\n"
            f"holds {prefix}_tilemap_code (32768 -> ~3500) instead.\n"
            "\n"
            "zaxxon::zaxxon() unpacks every COMPRESSED asset eagerly, largest\n"
            "first: Arena fills block A before B, so access order is\n"
            "placement order:\n"
            f"  {prefix}_bgtiles        (65536) -> A (A: 65536)\n"
            f"  {prefix}_spritetiles    (65536) -> B (B: 65536)\n"
            f"  {prefix}_tilemap_code   (32768) -> B (B: 98304)\n"
            f"  {prefix}_tilemap_color  (16384) -> A (A: 81920)\n"
            f"  {prefix}_palette          (512) -> A (A: 82432)\n"
            f"  {prefix}_fgcolor_codes    (256) -> A (A: 82688)")

  # ---- gfx_tx: chars, 2bpp planar 8x8 ----
  char_tiles = decode_char_tiles(roms["gfx_tx_1"] + roms["gfx_tx_2"])
  _write_char_tiles(ctx, prefix, char_tiles)
  ctx.log.info(f"char tiles: {len(char_tiles)}")

  # ---- gfx_bg: background tile graphics, 3bpp planar 8x8 ----
  bg_tiles = decode_bg_tiles(roms["gfx_bg_1"] + roms["gfx_bg_2"] + roms["gfx_bg_3"])
  _write_bg_tiles(ctx, prefix, bg_tiles)
  ctx.log.info(f"bg tiles: {len(bg_tiles)}")

  # ---- gfx_spr: sprites, 32x32 3bpp, zaxxon_spritelayout ----
  sprite_tiles = decode_sprite_tiles(roms["gfx_spr_1"] + roms["gfx_spr_2"] + roms["gfx_spr_3"])
  _write_sprite_tiles(ctx, prefix, sprite_tiles)
  ctx.log.info(f"sprite tiles: {len(sprite_tiles)}")

  # ---- tilemap_dat: fixed 32x512 bg playfield map, not gfx data ----
  # zaxxon_state::get_bg_tile_info: source split in half; low byte = tile
  # code, high half byte = (code_hi<<8 | color<<4). size = region/2.
  tilemap_dat = roms["tilemap_1"] + roms["tilemap_2"] + roms["tilemap_3"] + roms["tilemap_4"]
  assert len(tilemap_dat) == 0x8000
  size = len(tilemap_dat) // 2
  assert size == 32 * 512
  tile_code  = [tilemap_dat[i] + 256 * (tilemap_dat[i + size] & 3) for i in range(size)]
  tile_color = [tilemap_dat[i + size] >> 4 for i in range(size)]
  assert max(tile_code) < len(bg_tiles)
  _write_tilemap(ctx, prefix, tile_code, tile_color)
  ctx.log.info(f"tilemap: {size} cells (32x512)")

  # ---- palette ----
  prom_colors, prom_fgsel = roms["prom_colors"], roms["prom_fgsel"]
  assert len(prom_colors) == 256 and len(prom_fgsel) == 256
  rgb565 = _convert_palette(prom_colors)
  _write_palette(ctx, prefix, rgb565, prom_fgsel)
  ctx.log.info("palette: 256 colors + 256 fg color-select entries")

def _write_ops(ctx, prefix, ops):
  # PLAIN and unplanned: read only by opcode fetches, and the Arena
  # budget above has no room for another 20480 bytes
  _write_rom(ctx, f"{prefix}_rom_ops.h", f"{prefix}_rom_ops", ops,
            "Decrypted opcode image 0x0000-0x4fff (MAME AS_OPCODES), read by\n"
            "opZ80/opPrefixZ80; rom_main holds the decrypted data image.", plan=False)

def _convert(ctx: Context) -> None:
  _convert_set(ctx, "zaxxon", ZAXXON_FILES,
               "Zaxxon main CPU ROM 0x0000-0x4fff (20KB), not encrypted (US rev D).")
  ctx.log.info("Zaxxon (ROM/gfx) conversion done.")
  _write_samples(ctx)
  ctx.log.info("Zaxxon (audio) conversion done.")


def _convert_sz(ctx: Context) -> None:
  _convert_set(ctx, "szaxxon", _SZAXXON_FILES,
               "Super Zaxxon main CPU data image 0x0000-0x4fff (20KB), 315-5013 decrypted.",
               segacrpt.TABLE_315_5013)
  ctx.log.info("Super Zaxxon conversion done (samples shared with zaxxon).")


JOBS = (
    Job(name="zaxxon", convert=_convert, machine="zaxxon", zips=("zaxxon.zip", _AUDIO_ZIP), plan=True),
    Job(name="szaxxon", convert=_convert_sz, machine="szaxxon", zips=("szaxxon.zip",), plan=True,
        deps=("zaxxon",)),
)
