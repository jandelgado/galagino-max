"""The Tower of Druaga (Namco 1984): the Mappy converter with 64 sprite color groups."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.emit.write import write_bytes
from romconv.games.namco.common import (interleaved_sprites, inverted_tiles, sprite_words, tile_rows,
                                        tile_tables, write_wsg)
from romconv.gfx.color import rgb565_swapped_packed
from romconv.hw.namco import nudge

_SPRITE_GROUPS = 64


def _write_tiles(ctx: Context, tiles) -> None:
    with ctx.header("todruaga_tilemap.h") as f:
        print("// Tower of Druaga tiles (td1_5.3b, ROMREGION_INVERT) — 256 tile 8x8 2bpp", file=f)
        print("// pixel LSB-first come galaga_tilemap (blit: (pix>>2c)&3)", file=f)
        emit_compressed(f, "todruaga_tilemap", "unsigned short", "[8]", len(tiles), tile_rows(tiles))


def _write_sprites(ctx: Context, sprites) -> None:
    # non flippati: todruaga::blit_sprite applica il flip a runtime
    with ctx.header("todruaga_spritemap.h") as f:
        print("// Tower of Druaga sprites (td1_6.3m+td1_7.3n interallacciate) — 128 sprite 16x16 4bpp", file=f)
        print("// [codice][riga*2+meta']: nibble LSB-first,", file=f)
        print("// [2r]=pixel 0-7, [2r+1]=pixel 8-15 (stile 1942 4bpp)", file=f)
        emit_compressed(f, "todruaga_sprites", "uint32_t", "[32]", len(sprites), sprite_words(sprites))


def _write_colormaps(ctx: Context, pal_prom, char_lut, spr_lut) -> None:
    pal = [rgb565_swapped_packed(c) for c in pal_prom]  # 32 colori
    with ctx.header("todruaga_cmap.h") as f:
        print("// Colormap Tower of Druaga da td1-6.4c (char, pen+0x10) e td1-7.5k", file=f)
        print("// (sprite, 0x400 byte = 64 gruppi x 16 — todruaga ha 64 gruppi", file=f)
        print("// sprite contro i 16 di mappy). Trasparenza hardware = lookup 0xF:", file=f)
        print("//  - tiles opachi: colore reale (prima passata, tilemap OPACO)", file=f)
        print("//  - tiles_prio: 0x0000 dove lookup==0xF (ridisegno tile prioritari", file=f)
        print("//    sopra gli sprite: attr bit6, si salta il colore 'trasparente')", file=f)
        print("//  - sprites: 0x0000 dove lookup==0xF (pixel non disegnato)", file=f)
        tile_tables(f, "todruaga", pal, char_lut)
        flat = []
        for g in range(_SPRITE_GROUPS):
            for p in range(16):
                lut = spr_lut[g * 16 + p] & 0x0f
                flat.append(0 if lut == 0x0f else nudge(pal[lut]))
        emit_compressed(f, "todruaga_colormap_sprites", "unsigned short", "[16]", _SPRITE_GROUPS, flat)


def _convert(ctx: Context) -> None:
    # sha1 from MAME ROM_START(todruaga) "New Ver."
    main_hi = ctx.rom("td2_3.1d", "9abbaaaf0a53aff38df8287f62d091b13146cf13")
    main_lo = ctx.rom("td2_1.1b", "ab8eadd45638ff1ab2dacbd5ab2c6870b9f79086")
    sub = ctx.rom("td1_4.1k", "3d8621fdd74fafa61f342886faa37f0aab50c5a7")
    tiles = ctx.rom("td1_5.3b", "7d7cee4101ef615fb92c3702f89a9823a6231195")
    spr_even = ctx.rom("td1_6.3m", "74e0af4c7d6e334bcd211a33eb18dddc8a182aa7")
    spr_odd = ctx.rom("td1_7.3n", "74cdcafc26475bda085bf62ed17e6474ed782453")
    pal = ctx.rom("td1-5.5b", "a648c53f2e95634bb5b27d79be3fd908021d056e")
    char_lut = ctx.rom("td1-6.4c", "1340e4f657f4f2c4ef651a441c3b51632e757d0b")
    spr_lut = ctx.rom("td1-7.5k", "dfd7d6b2740761c3bcab4c7999d2699d920843e7")
    wave = ctx.rom("td1-3.3m", "16db55525034bacb71e7dc8bd2a7c3c4464d4808")

    _write_tiles(ctx, inverted_tiles(tiles))
    # sprites: stream interallacciato 16 bit (td1_6 = byte pari, td1_7 = dispari)
    _write_sprites(ctx, interleaved_sprites(spr_even, spr_odd, 0x4000, 128))
    _write_colormaps(ctx, pal, char_lut, spr_lut)
    # 256 byte, 4 bit bassi = 8 forme d'onda x 32 campioni, centrate (-8..7)
    write_wsg(ctx, "todruaga_wavetable.h", "todruaga_wavetable",
              "Tower of Druaga WSG 15XX waveforms (td1-3.3m): 8 waves x 32 samples", wave)

    write_bytes(ctx, "todruaga_rom_main.h", "todruaga_rom_main", main_hi + main_lo,
              "Tower of Druaga main M6809 ROM 0x8000-0xFFFF (td2_3.1d+td2_1.1b)")
    write_bytes(ctx, "todruaga_rom_sub.h", "todruaga_rom_sub", sub,
              "Tower of Druaga sound M6809 ROM 0xE000-0xFFFF (td1_4.1k)")
    ctx.log.info("Tower of Druaga conversion finished.")


JOB = Job(name="todruaga", convert=_convert, machine="todruaga", zips=("todruaga.zip",))
