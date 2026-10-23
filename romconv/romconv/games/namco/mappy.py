"""Mappy (Namco 1983)."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.emit.write import write_bytes
from romconv.games.namco.common import (interleaved_sprites, inverted_tiles, sprite_words, tile_rows,
                                        tile_tables, write_wsg)
from romconv.gfx.color import rgb565_swapped_packed
from romconv.hw.namco import nudge

_SPRITE_GROUPS = 16


def _write_tiles(ctx: Context, tiles) -> None:
    with ctx.header("mappy_tilemap.h") as f:
        print("// Mappy tiles (mp1_5.3b, ROMREGION_INVERT) — 256 tile 8x8 2bpp", file=f)
        print("// pixel LSB-first come galaga_tilemap (blit: (pix>>2c)&3)", file=f)
        emit_compressed(f, "mappy_tilemap", "unsigned short", "[8]", len(tiles), tile_rows(tiles))


def _write_sprites(ctx: Context, sprites) -> None:
    # non flippati: mappy::blit_sprite applica il flip a runtime
    with ctx.header("mappy_spritemap.h") as f:
        print("// Mappy sprites (mp1_6.3m+mp1_7.3n interallacciate) — 128 sprite 16x16 4bpp", file=f)
        print("// [codice][riga*2+meta']: nibble LSB-first,", file=f)
        print("// [2r]=pixel 0-7, [2r+1]=pixel 8-15 (stile 1942 4bpp)", file=f)
        emit_compressed(f, "mappy_sprites", "uint32_t", "[32]", len(sprites), sprite_words(sprites))


def _write_colormaps(ctx: Context, pal_prom, char_lut, spr_lut) -> None:
    pal = [rgb565_swapped_packed(c) for c in pal_prom]  # 32 colori
    with ctx.header("mappy_cmap.h") as f:
        print("// Colormap Mappy da mp1-6.4c (char, pen+0x10) e mp1-7.5k (sprite)", file=f)
        print("// Trasparenza hardware = nibble lookup 0xF:", file=f)
        print("//  - tiles opachi: colore reale (prima passata, tilemap OPACO)", file=f)
        print("//  - tiles_prio: 0x0000 dove lookup==0xF (ridisegno tile prioritari", file=f)
        print("//    sopra gli sprite: attr bit6, si salta il colore 'trasparente')", file=f)
        print("//  - sprites: 0x0000 dove lookup==0xF (pixel non disegnato)", file=f)
        tile_tables(f, "mappy", pal, char_lut)
        print("const unsigned short mappy_colormap_sprites[][16] = {", file=f)
        rows = []
        for g in range(_SPRITE_GROUPS):
            vals = []
            for p in range(16):
                lut = spr_lut[g * 16 + p] & 0x0f
                vals.append(hex(0) if lut == 0x0f else hex(nudge(pal[lut])))
            rows.append("{" + ",".join(vals) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)


def _convert(ctx: Context) -> None:
    cpu01 = ctx.rom("mpx_3.1d", "b9722941438e93325e84691ada4e95620bec73b2")
    cpu02 = ctx.rom("mp1_2.1c", "e5198703cdf47b2cd7fc9f2a5fde7bf4ab2275db")
    cpu03 = ctx.rom("mpx_1.1b", "1dbc4f42d4c16a08240a221bec27dcc3a8dd7461")
    subcpu = ctx.rom("mp1_4.1k", "f36b57f7f1e79f00b3f07afe1960bca5f5325ee2")
    tiles1 = ctx.rom("mp1_5.3b", "76610149c65f955484fef1c033ddc3fed3f4e568")
    sprites1 = ctx.rom("mp1_6.3m", "3cc216793c6a5f73c437ad2524563deb3b5e2890")
    sprites2 = ctx.rom("mp1_7.3n", "8dfbf03953d5219d9eb5fc654ec3392442ba1dc4")
    proms1 = ctx.rom("mp1-5.5b", "2e356706c07f43eeb67783fb122bdc7fed1b3589")
    proms2 = ctx.rom("mp1-6.4c", "f578e14f15783acb2073644db4a2f0d196cc0957")
    proms3 = ctx.rom("mp1-7.5k", "2e387e5d8b8cab005f67f821b4db65d0ae8bd362")
    proms4 = ctx.rom("mp1-3.3m", "847cbaf7c88616576c410177e066ae1d792ac0ba")

    _write_tiles(ctx, inverted_tiles(tiles1))
    # sprites: stream interlaced 16 bit (mp1_6 = even bytes, mp1_7 = odd bytes)
    _write_sprites(ctx, interleaved_sprites(sprites1, sprites2, 0x4000, 128))
    _write_colormaps(ctx, proms1, proms2, proms3)
    # 256 byte, 4 bit bassi = 8 forme d'onda x 32 campioni, centrate (-8..7)
    write_wsg(ctx, "mappy_wavetable.h", "mappy_wavetable",
              "Mappy WSG 15XX waveforms (mp1-3.3m): 8 forme x 32 campioni", proms4)

    write_bytes(ctx, "mappy_rom_main.h", "mappy_rom_main", cpu01 + cpu02 + cpu03,
              "Mappy main M6809 ROM 0xA000-0xFFFF (mpx_3.1d+mp1_2.1c+mpx_1.1b)")
    write_bytes(ctx, "mappy_rom_sub.h", "mappy_rom_sub", subcpu,
              "Mappy sound M6809 ROM 0xE000-0xFFFF (mp1_4.1k)")
    ctx.log.info("Mappy conversion finished.")


JOB = Job(name="mappy", convert=_convert, machine="mappy", zips=("mappy.zip",))
