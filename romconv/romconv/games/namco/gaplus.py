"""Gaplus / Galaga 3 (Namco 1984).

The romset is a hybrid (code of "galaga3" GP3, gfx1 and the two sprite color PROMs of "gaplus"
GP2): the converter uses whichever zip is present, gaplus.zip first. The hardware is the twin of
galaga (same tile address, tile/attr split) and mappy/todruaga (same namcoio 56XX/58XX, WSG 15XX,
ROT90 sprites), but sprite sizes are independent and there is a "duplicate" flag."""
import wave
from types import MappingProxyType

import numpy as np

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.games.namco.common import sprite_words, tile_rows, write_rom, write_wsg
from romconv.gfx.adpcm import emit_adpcm
from romconv.gfx.color import rgb565_swapped_scaled
from romconv.gfx.gfxutil import mame_decode, rot_galagino
from romconv.hw.namco import nudge

_GAPLUS_ZIP = "gaplus.zip"
_GALAGA3_ZIP = "galaga3.zip"

_SHARED = MappingProxyType({
    "subcpu2": ("gp2-7.11c", "b86020f819fefb134cb57e203f7c90b1b29581c8"),
    "soundcpu1": ("gp2-1.4b", "4e0a31d84cb7aca497485dbe0240009d58275765"),
    "gfx2_1": ("gp2-11.11p", "16873e0ac5f975768d596d7d32af7571f4817f2b"),
    "gfx2_2": ("gp2-10.11n", "fc346e98737c9fc20810e32d4c150ae4b4051979"),
    "gfx2_3": ("gp2-12.11r", "368e4541a5151e906a189712bc05192c2ceec8ae"),
    "gfx2_4": ("gp2-9.11m", "99c1e67c3b216aa1b63f199e21c73cdedde80e1b"),
    "proms1": ("gp2-3.1p", "dcd6dfbfbd5281ba0c7b7c189d6fde23617ed3e3"),   # red palette ROM (4 bits)
    "proms2": ("gp2-1.1n", "c76f9d9b066e268621d41a703c5280261234709a"),   # green palette ROM
    "proms3": ("gp2-2.2n", "64d7b333f529d3ba66aeefd380fd1cbf9ddf460d"),   # blue palette ROM
    "proms4": ("gp2-7.6s", "781ffe9088476798409cb922350eff881590cf35"),   # char color ROM
    "soundprom": ("gp2-4.3f", "e6a23cd5ce3d3e76de3b70c8ab5a3c45b1147af4"),
    "plds1": ("pal10l8.8n", "1aa7fa1a61795703af84ae427d0d8588ef8c4c3f"),  # checked, not converted
})
# key -> (name, sha1) per romset
_GAPLUS = MappingProxyType({
    **_SHARED,
    "maincpu1": ("gp2-4.8d", "93fcd8b940491abf6344181811d0b35765d7e45c"),
    "maincpu2": ("gp2-3b.8c", "81402b28a2d5ac2d1301252534afa0cb65d7e162"),
    "maincpu3": ("gp2-2b.8b", "025c2f3978772e1ecbbf36842dc7c2203ee91a1f"),
    "subcpu1": ("gp2-8.11d", "f230eb0ad757f0714c0ac81c812e950778452947"),
    "subcpu3": ("gp2-6.11b", "398059da967c80321a9ec94d982a6c0b3c970c5f"),
    "gfx1_1": ("gp2-5.8s", "a0107fa4659597ac42c875ab1c0deb845534268b"),   # characters
    "proms5": ("gp2-6.6p", "955dcef363870ee8e91edc73b9ea3ce489738aad"),   # sprite color ROM (lower 4 bits)
    "proms6": ("gp2-5.6n", "a93a5bc448dc127e1389d10a9cb06acadfe940cf"),   # sprite color ROM (upper 4 bits)
})
_GALAGA3 = MappingProxyType({
    **_SHARED,
    "maincpu1": ("gp3-4c.8d", "e39f77af16016d28170e4ac1c2a784b0a7ec5454"),
    "maincpu2": ("gp3-3c.8c", "2b6bb2a5d77a837810180391ef6c0ce745bfed64"),
    "maincpu3": ("gp3-2d.8b", "b176b46bd6f2501d3a74ed11186be8411fd1105b"),
    "subcpu1": ("gp3-8b.11d", "bbed2056dc28dc2828e29987c16d89fb16e7059e"),
    "subcpu3": ("gp3-6b.11b", "a19f2942dafc899d686a42240fc2f7a7a7d3b1f5"),
    "gfx1_1": ("gp3-5.8s", "0a556b45976bc36eb99048b1512c446b472da1d2"),
    "proms5": ("gp3-6.6p", "6d0512958bc522d22e69336677369507847f8f6f"),
    "proms6": ("gp3-5.6n", "2ba51ccdd0428fc48758ed8fea36c8ce0e752a45"),
})

# layout MAME (absolute bit offsets in the buffer rebuilt like driver_init does)
# charlayout: 8x8x2bpp, planeoffset={4,6}, 32 bytes per char
_CHAR_PLANES = (4, 6)
_CHAR_XOFFS = (16 * 8, 16 * 8 + 1, 24 * 8, 24 * 8 + 1, 0, 1, 8 * 8, 8 * 8 + 1)
_CHAR_YOFFS = tuple(y * 8 for y in range(8))
_CHAR_BITS_PER_TILE = 32 * 8
_CHAR_COUNT = 512  # 0x4000 bytes after driver_init / 32

# spritelayout: 16x16x3bpp RGN_FRAC(1,2), planeoffset={RGN_FRAC(1,2),0,4}
_SPR_XOFFS = (0, 1, 2, 3, 8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
              16 * 8, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3, 24 * 8, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3)
_SPR_YOFFS = tuple(y * 8 for y in range(8)) + tuple(32 * 8 + y * 8 for y in range(8))
_SPR_BITS_PER_TILE = 64 * 8
_SPR_COUNT = 384  # 0x6000 (half of the region) / 64 bytes

_MAX_STARS = 250
_STARFIELD_CLIPPING_X = 16
_SCREEN_W, _SCREEN_H = 288, 224
_STAR_COLOR_BASE = (0x250, 0x230, 0x210)

_SAMPLE_RATE = 24000
_SAMPLE_PEAK = 127.0


def _write_tiles(ctx: Context, tiles) -> None:
    with ctx.header("gaplus_tilemap.h") as f:
        print("// Gaplus tiles (gp2-5.8s + driver_init unpack nibble) — 512 tile 8x8 2bpp", file=f)
        emit_compressed(f, "gaplus_tilemap", "unsigned short", "[8]", len(tiles), tile_rows(tiles))


def _write_sprites(ctx: Context, sprites) -> None:
    # Unflipped, gaplus::blit_sprite flips at draw time.
    with ctx.header("gaplus_spritemap.h") as f:
        print("// Gaplus sprites (gp2-11+gp2-10+gp2-12+gp2-9 + driver_init unpack) —", file=f)
        emit_compressed(f, "gaplus_sprites", "uint32_t", "[32]", len(sprites), sprite_words(sprites))


def _decode_palette(red_prom, green_prom, blue_prom) -> list[int]:
    # gaplus_palette(): weighted resistors 0x0e/0x1f/0x43/0x8f on the 4 bits
    def comp(byte):
        v = 0
        if byte & 1:
            v += 0x0e
        if byte & 2:
            v += 0x1f
        if byte & 4:
            v += 0x43
        if byte & 8:
            v += 0x8f
        return v

    return [rgb565_swapped_scaled(comp(red_prom[i]), comp(green_prom[i]), comp(blue_prom[i]))
            for i in range(256)]


def _write_colormaps(ctx: Context, pal, char_lut, spr_lut_lo, spr_lut_hi) -> None:
    with ctx.header("gaplus_cmap.h") as f:
        print("// Colormap Gaplus da gp2-6s.bin (char lut, 64 gruppi x4 pen,", file=f)
        print("// pen = 0xF0+lut, SOLO 16 colori fisici usati per tutto il testo)", file=f)
        print("// e gp2-6n.bin/gp2-6p.bin (sprite lut upper/lower nibble, 64 gruppi", file=f)
        print("// x8 pen, colore DIRETTO lut_lo|lut_hi<<4 = indice 0-255 nella", file=f)
        print("// stessa tavolozza RGB della palette -- NON indiretto come mappy).", file=f)
        print("// Trasparenza hardware: MAME chiama configure_groups(gfx(0),0xff)", file=f)
        print("// -- QUALSIASI pixel char il cui lut (nibble basso) vale 0x0F", file=f)
        print("// risolve a pen indiretto 0xF0+0x0F=0xFF ed e' TRASPARENTE in", file=f)
        print("// ENTRAMBE le passate (non solo quella prioritaria): le stelle", file=f)
        print("// dello starfield, disegnate PRIMA della tilemap, devono restare", file=f)
        print("// visibili nei buchi. Sprite -> pen 0xFF trasparente (separato).", file=f)

        # the opaque and the priority table are the same here
        for table in ("gaplus_colormap_tiles", "gaplus_colormap_tiles_prio"):
            print(f"const unsigned short {table}[][4] = {{", file=f)
            rows = []
            for g in range(64):
                vals = []
                for p in range(4):
                    lut = char_lut[g * 4 + p] & 0x0f
                    vals.append(hex(0) if lut == 0x0f else hex(nudge(pal[0xf0 + lut])))
                rows.append("{" + ",".join(vals) + "}")
            print(",\n".join(rows), file=f)
            print("};", file=f)

        flat = []
        for g in range(64):
            for p in range(8):
                idx = g * 8 + p
                lut = (spr_lut_lo[idx] & 0x0f) | ((spr_lut_hi[idx] & 0x0f) << 4)
                flat.append(0 if lut == 0xff else nudge(pal[lut]))
        emit_compressed(f, "gaplus_colormap_sprites", "unsigned short", "[8]", 64, flat)


def _gen_starfield() -> list[tuple[int, int, int, int]]:
    # from gaplus_v.cpp starfield_init() (LFSR 32bit)
    stars = []
    generator = 0
    sett = 0
    for y in range(_SCREEN_H):
        for x in range(_SCREEN_W - _STARFIELD_CLIPPING_X * 2 - 1, -1, -1):
            generator = (generator << 1) & 0xFFFFFFFF
            bit1 = (~generator >> 17) & 1
            bit2 = (generator >> 5) & 1
            if bit1 ^ bit2:
                generator |= 1
            if ((~generator) >> 16) & 1 and (generator & 0xff) == 0xff:
                color = ((~(generator >> 8)) & 0xFFFFFFFF) % 7 + 1
                color_base = _STAR_COLOR_BASE[sett]
                if color and len(stars) < _MAX_STARS:
                    stars.append((x + _STARFIELD_CLIPPING_X, y, color_base + color, sett))
                    sett = (sett + 1) % 3
    return stars


def _write_starfield(ctx: Context, stars, pal, spr_lut_lo, spr_lut_hi) -> None:
    with ctx.header("gaplus_starseed.h") as f:
        print("struct gaplus_star_S { unsigned short x, y; unsigned short col; unsigned char set; };", file=f)
        print(f"#define GAPLUS_NUM_STARS {len(stars)}", file=f)
        print("const struct gaplus_star_S gaplus_starseed[] = {", file=f)
        rows = []
        for (x, y, penidx, sett) in stars:
            local = penidx - 256
            lut = (spr_lut_lo[local] & 0x0f) | ((spr_lut_hi[local] & 0x0f) << 4)
            col = 0 if lut == 0xff else nudge(pal[lut])
            rows.append(f" {{ {x}, {y}, {hex(col)}, {sett} }}")
        print(",\n".join(rows), file=f)
        print("};", file=f)


def _write_sample_bang(ctx: Context) -> None:
    path = ctx.paths.samples / "gaplus_bang.wav"
    if not path.exists():
        raise ConversionError("gaplus_bang.wav missing, gaplus_sample_bang.h not generated")
    with wave.open(str(path), "rb") as w:
        nch, sw, fr, nframes = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        frames = w.readframes(nframes)
    dtype = {1: np.int8, 2: "<i2", 4: "<i4"}[sw]
    data = np.frombuffer(frames, dtype=dtype).astype(np.float64)
    if nch > 1:
        data = data.reshape(-1, nch).mean(axis=1)

    n_out = int(len(data) * _SAMPLE_RATE / fr)
    x_old = np.linspace(0, 1, len(data), endpoint=False)
    x_new = np.linspace(0, 1, n_out, endpoint=False)
    resampled = np.interp(x_new, x_old, data)

    peak = np.abs(resampled).max()
    scale = _SAMPLE_PEAK / peak if peak > 0 else 1.0
    samples8 = np.clip(np.round(resampled * scale), -128, 127).astype(np.int8)

    with ctx.header("gaplus_sample_bang.h") as f:
        print("// Explosion samples for \"bang\" (samples/gaplus_bang.wav),", file=f)
        print("// resampled @24000 Hz signed 8bit, stored as 4-bit IMA-ADPCM", file=f)
        print("// (ima_adpcm::Voice). COMPRESSED: the Arena has room.", file=f)
        print(f"// {len(samples8)} samples ({len(samples8)/_SAMPLE_RATE:.3f}s a 24000 Hz)", file=f)
        emit_adpcm(f, "gaplus_sample_bang", samples8, compress=True)
    ctx.log.info(f"gaplus_sample_bang.h: {len(samples8)} samples @24kHz ({len(samples8)/_SAMPLE_RATE:.3f}s)")


def _convert(ctx: Context) -> None:
    if ctx.zip_path(_GAPLUS_ZIP).is_file():
        zip, files = _GAPLUS_ZIP, _GAPLUS
    elif ctx.zip_path(_GALAGA3_ZIP).is_file():
        zip, files = _GALAGA3_ZIP, _GALAGA3
    else:
        raise ConversionError(f"{_GAPLUS_ZIP} or {_GALAGA3_ZIP}: No roms.")

    def rom(key: str) -> bytearray:
        name, sha1 = files[key]
        return ctx.rom(name, sha1, zip)

    main_rom = rom("maincpu1") + rom("maincpu2") + rom("maincpu3")
    sub_rom = rom("subcpu1") + rom("subcpu2") + rom("subcpu3")
    soundcpu = rom("soundcpu1")
    char_src = rom("gfx1_1")
    gfx2 = [rom(f"gfx2_{i}") for i in range(1, 5)]
    proms1, proms2, proms3 = rom("proms1"), rom("proms2"), rom("proms3")
    proms4, proms5, proms6 = rom("proms4"), rom("proms5"), rom("proms6")
    soundprom = rom("soundprom")
    rom("plds1")

    # gfx1 (chars): replicates driver_init, the upper half is the lower nibble shifted down
    char_full = bytearray(0x4000)
    char_full[0:0x2000] = char_src
    for i in range(0x2000):
        char_full[0x2000 + i] = char_src[i] >> 4
    tiles = [rot_galagino(t) for t in
             mame_decode(bytes(char_full), 8, 8, _CHAR_PLANES, _CHAR_XOFFS, _CHAR_YOFFS,
                         _CHAR_BITS_PER_TILE, _CHAR_COUNT)]
    _write_tiles(ctx, tiles)

    # gfx2 (sprites): 4 ROMs of 8KB @0/0x2000/0x4000/0x6000, then rom[0x8000+i] = rom[0x6000+i]<<4
    # for i < 0x2000, 0xA000-0xBFFF zero (ROM_FILL, "optional, unused")
    spr_full = bytearray(0xC000)
    for i, data in enumerate(gfx2):
        spr_full[i * 0x2000:(i + 1) * 0x2000] = data
    for i in range(0x2000):
        spr_full[0x8000 + i] = (gfx2[3][i] << 4) & 0xff

    half_bits = (0xC000 * 8) // 2  # RGN_FRAC(1,2)
    spr_planes = (half_bits, 0, 4)
    sprites = [rot_galagino(s) for s in
               mame_decode(bytes(spr_full), 16, 16, spr_planes, _SPR_XOFFS, _SPR_YOFFS,
                           _SPR_BITS_PER_TILE, _SPR_COUNT)]
    _write_sprites(ctx, sprites)

    pal = _decode_palette(proms1, proms2, proms3)
    _write_colormaps(ctx, pal, proms4, proms5, proms6)
    write_wsg(ctx, "gaplus_wavetable.h", "gaplus_wavetable",
              "Gaplus WSG 15XX waveforms: 8 waves x 32 samples", soundprom)

    assert len(main_rom) == 0x6000
    write_rom(ctx, "gaplus_rom_main.h", "gaplus_rom_main", main_rom, "Gaplus main M6809 ROM 0xA000-0xFFFF")
    assert len(sub_rom) == 0x6000
    write_rom(ctx, "gaplus_rom_sub.h", "gaplus_rom_sub", sub_rom, "Gaplus sub M6809 ROM 0xA000-0xFFFF")
    assert len(soundcpu) == 0x2000
    write_rom(ctx, "gaplus_rom_sub2.h", "gaplus_rom_sub2", soundcpu,
              "Gaplus sound (sub2) M6809 ROM 0xE000-0xFFFF")

    stars = _gen_starfield()
    _write_starfield(ctx, stars, pal, proms5, proms6)
    ctx.log.info(f"Starfield: {len(stars)} stars generated")

    _write_sample_bang(ctx)
    ctx.log.info("Gaplus conversion finished.")


JOB = Job(name="gaplus", convert=_convert, machine="gaplus", zips=(_GAPLUS_ZIP, _GALAGA3_ZIP))
