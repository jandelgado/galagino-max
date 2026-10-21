# ============================================================
# Circus Charlie (Konami 1984) ROM converter
#
# Hardware (MAME konami/circusc.cpp):
# - tiles 8x8 4bpp "packed msb" (gfx_8x8x4_packed_msb): 32 byte/tile,
#   2 pixel/byte, nibble ALTO = pixel di sinistra; 512 tile da 16KB
# - sprite 16x16 4bpp packed msb: 128 byte/sprite, 384 sprite da 48KB
# - palette PROM 32B bbgggrrr (identica a mappy) + 2 lookup 256B:
#   char pen = lut+0x10, sprite pen = lut; trasparenza sprite =
#   transpen_mask(color, 0): pen p trasparente se lut[c*16+p]==lut[c*16+0]
#
# Formati output:
# - tiles PRE-RUOTATI ROT90 galagino (out[y][x] = mame[7-x][y], come
#   mappy/todruaga) e impacchettati [8][4] nibble LSB-first (nibble basso
#   = colonna pari) -> blit stile timeplt con ptr[c]
# - sprite in ORIENTAMENTO LANDSCAPE nativo [16][8] nibble LSB-first,
#   senza varianti flip (48KB una copia sola): la rotazione la fa il blit
#   trasposto (screen_x = spr_x + 15 - r) e i flip si fanno a runtime,
#   come rocnrope
# ============================================================
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.gfx.color import rgb565_swapped_packed
from romconv.gfx.gfxutil import rot_galagino
from romconv.hw.namco import nudge

_REQUIRED = (
    # main CPU (KONAMI-1), 0x6000-0xFFFF
    ("380_s05.3h", "0e5bd350fa5fee42569eb0c4accf7512d645b792"),  # @6000
    ("380_q04.4h", "458c398911453d558003f49c298b0d593c941c11"),  # @8000
    ("380_q03.5h", "03211f0cc90b6e356989c5e2a41b70f4ff2ead83"),  # @a000
    ("380_q02.6h", "a1f65e73c4e5abff1b0970bad32a128173245561"),  # @c000
    ("380_q01.7h", "2f40e1a109d129bb127a8b98e27817988cd08c8b"),  # @e000
    # audio CPU (Z80), 0x0000-0x3FFF
    ("380_l14.5c", "67103d61994fd3a1e2de7cf9487e4f763234b18e"),
    ("380_l15.7c", "14f305717edcc2471e763b262960a0b96eef3530"),
    # tiles
    ("380_j12.4a", "73b9e3d46dfe9e39b390c634df153648a0906876"),
    ("380_j13.5a", "4d0b0a773c385b7f1dcf024760d0437f47e78fbe"),
    # sprites
    ("380_j06.11e", "70a50dcc86dfbdaa9c2af613105aae7f90747804"),
    ("380_j07.12e", "2ad7cbcbdbb434dc43e9c94cd00df9e57ac097f5"),
    ("380_j08.13e", "b22ad7cfda392894208eb4b39505f38bfe4c4342"),
    ("380_j09.14e", "1a649ec667d377ffab26b4694be790b3a2742f30"),
    ("380_j10.15e", "4c02b75a62993cce60d2cb87b81c7738abbc9a0d"),
    ("380_j11.16e", "d315588e6cc2f4263be621d2d8603c8215a90046"),
    # PROM
    ("380_j18.2a", "599acd25f36445221c553510a5de23ddba5ecc15"),  # palette
    ("380_j17.7b", "0d61d468f6d3e1570fd18d236ec8cab92db4ed5c"),  # char lut
    ("380_j16.10c", "86df21c8e0b1ed51a0a4bd33dbb33f6efdea7d39"),  # sprite lut
)


# ------------------------------------------------------------
# decode 4bpp "packed msb": 2 pixel/byte, nibble alto = pixel sinistro
# ------------------------------------------------------------
def _decode_packed(data, base, w, h):
    tile = []
    for y in range(h):
        row = []
        for x in range(w):
            b = data[base + y * (w // 2) + (x >> 1)]
            row.append((b >> 4) & 0xF if (x & 1) == 0 else b & 0xF)
        tile.append(row)
    return tile


# ------------------------------------------------------------
# scritture header
# ------------------------------------------------------------
def _write_tiles(ctx, tiles):
    # pre-ruotati; [8][4] nibble LSB-first (nibble basso = colonna pari)
    with ctx.header("circusc_tilemap.h") as f:
        print("// Circus Charlie tiles — 512 tile 8x8 4bpp", file=f)
        print("// PRE-ROTATED ROT90 galagino; nibble LSB-first:", file=f)
        print("//   px = (tile[r][c>>1] >> ((c&1)*4)) & 0xF", file=f)
        flat = []
        for t in tiles:
            for y in range(8):
                for xb in range(4):
                    flat.append(t[y][2 * xb] | (t[y][2 * xb + 1] << 4))
        emit_compressed(f, "circusc_tilemap", "unsigned char", "[8][4]", len(tiles), flat)


def _write_sprites(ctx, sprites):
    # orientamento LANDSCAPE nativo (rotazione nel blit trasposto), una
    # sola copia (flip a runtime): [16][8] nibble LSB-first
    with ctx.header("circusc_spritemap.h") as f:
        print("// Circus Charlie sprites — 384 sprite 16x16 4bpp", file=f)
        print("// LANDSCAPE orientation, flip at runtime (like rocnrope):", file=f)
        print("//   px = (spr[row][col>>1] >> ((col&1)*4)) & 0xF", file=f)
        flat = []
        for s in sprites:
            for y in range(16):
                for xb in range(8):
                    flat.append(s[y][2 * xb] | (s[y][2 * xb + 1] << 4))
        emit_compressed(f, "circusc_spritemap", "unsigned char", "[16][8]", len(sprites), flat)


def _write_colormaps(ctx, pal_prom, char_lut, spr_lut):
    pal = [rgb565_swapped_packed(c) for c in pal_prom]
    with ctx.header("circusc_cmap.h") as f:
        print("// Colormap Circus Charlie da 380_j17.7b (char, pen+0x10) e 380_j16.10c", file=f)
        print("// (sprite). Tiles are OPAQUE.", file=f)
        print("// Sprite: trasparency = transpen_mask(c,0):", file=f)
        print("// pen p trasparent if lut[c*16+p]==lut[c*16+0] -> 0x0000", file=f)
        print("const unsigned short circusc_colormap_tiles[][16] = {", file=f)
        rows = []
        for g in range(16):
            vals = [hex(nudge(pal[16 + (char_lut[g * 16 + p] & 0x0f)])) for p in range(16)]
            rows.append("{" + ",".join(vals) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)
        print("const unsigned short circusc_colormap_sprites[][16] = {", file=f)
        rows = []
        for g in range(16):
            vals = []
            t0 = spr_lut[g * 16] & 0x0f
            for p in range(16):
                lut = spr_lut[g * 16 + p] & 0x0f
                vals.append(hex(0) if lut == t0 else hex(nudge(pal[lut])))
            rows.append("{" + ",".join(vals) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)


def _write_rom(ctx, name, sym, data, comment):
    with ctx.header(name) as f:
        print(f"// {comment}", file=f)
        emit_compressed(f, sym, "unsigned char", "", len(data), list(data))


# ------------------------------------------------------------
def _convert(ctx: Context) -> None:
    roms = {name: ctx.rom(name, sha1) for name, sha1 in _REQUIRED}

    # tiles: 16KB -> 512 tile pre-rotated
    tdata = roms["380_j12.4a"] + roms["380_j13.5a"]
    tiles = [rot_galagino(_decode_packed(tdata, 32 * t, 8, 8)) for t in range(512)]
    _write_tiles(ctx, tiles)

    # sprites: 48KB -> 384 sprites in landscape
    sdata = (roms["380_j06.11e"] + roms["380_j07.12e"] + roms["380_j08.13e"] +
             roms["380_j09.14e"] + roms["380_j10.15e"] + roms["380_j11.16e"])
    sprites = [_decode_packed(sdata, 128 * s, 16, 16) for s in range(384)]
    _write_sprites(ctx, sprites)

    _write_colormaps(ctx, roms["380_j18.2a"], roms["380_j17.7b"], roms["380_j16.10c"])

    main_rom = (roms["380_s05.3h"] + roms["380_q04.4h"] + roms["380_q03.5h"] + roms["380_q02.6h"] +
                roms["380_q01.7h"])

    _write_rom(ctx, "circusc_main_rom.h", "circusc_main_rom", main_rom,
               "Circus Charlie main KONAMI-1 ROM 0x6000-0xffff")

    _write_rom(ctx, "circusc_audio_rom.h", "circusc_audio_rom",
               roms["380_l14.5c"] + roms["380_l15.7c"],
               "Circus Charlie sound Z80 ROM 0x0000-0x3fff")

    ctx.log.info("Circus Charlie conversion finished.")


JOB = Job(name="circusc", convert=_convert, machine="circusc", zips=("circusc.zip",))
