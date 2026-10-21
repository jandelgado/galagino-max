"""Dig Dug II and Motos (Namco 1985).

Motos has the same ROM regions, sizes and gfx layouts as Dig Dug II (MAME mappy.cpp), only
other files. Same board as todruaga, except:
 - sprites 2x 16KB (d21_6.3m + d21_7.3n) = 256 sprites (todruaga 128)
 - sprite lookup d21-7.5k 0x100 bytes = 16 color groups (todruaga 64)"""
from functools import partial

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.emit.write import write_bytes
from romconv.games.namco.common import (interleaved_sprites, inverted_tiles, sprite_words, tile_rows,
                                        tile_tables, write_wsg)
from romconv.gfx.color import rgb565_swapped_packed
from romconv.hw.namco import nudge
from romconv.plan.budget import log_sizes

_NUM_SPRITES = 256
_SPRITE_GROUPS = 16

# (name, size, sha1) per MAME ROM_START: main hi, main lo, sub, tiles, sprites even, sprites odd,
# palette, char lookup, sprite lookup, waveform
_DIGDUG2 = (
    ("d23_3.1d", 0x4000, "d6796479ebb00081e9ae281380a4ce75f730766e"),
    ("d23_1.1b", 0x4000, "698a5c425e23627331d85216a4edee9c391e5749"),
    ("d21_4.1k", 0x2000, "0e46204089cc6e5ffab0d2a62f9a1728f8c35948"),
    ("d21_5.3b", 0x1000, "c9a54df22b0b92efbe7417a00200587225906b46"),
    ("d21_6.3m", 0x4000, "004fba630018dbf03c4b0e284c98077e19fface3"),
    ("d21_7.3n", 0x4000, "77d8d8e6039272f73e63c8f76084138ec613365a"),
    ("d21-5.5b", 0x0020, "77e840d10ab59708a051c3b15305b33d431ee06d"),
    ("d21-6.4c", 0x0100, "bd6bd641c9f220b6a2cc414a1117d5c089571400"),
    ("d21-7.5k", 0x0100, "30a4593726f5a4791e0812fd593e592087e730e3"),
    ("d21-3.3m", 0x0100, "f4f02977130110be52f4dd82fc3c0d02f45778b9"),
)
_MOTOS = (
    ("mo1_3.1d", 0x4000, "ade809a73ac24494b9f95f65b7592df5f86dce60"),
    ("mo1_1.1b", 0x4000, "b050495fcc4a4d93551b29d4f05e49f64017c870"),
    ("mo1_4.1k", 0x2000, "a8b195acfec542734751de29c9dafc2b165a5881"),
    ("mo1_5.3b", 0x1000, "4af9bf2ae9bb78d2d029ef38809181ffa3c0eb66"),
    ("mo1_6.3m", 0x4000, "664679f9d3d74a3fccb086af910392b4fe40c9bc"),
    ("mo1_7.3n", 0x4000, "2b49cdec516e23783f2a291633d81ab8bd0245fc"),
    ("mo1-5.5b", 0x0020, "66b0619affcc5168b099108800a941d6e2416ab0"),
    ("mo1-6.4c", 0x0100, "24eb167266752b064689662e3ef0f62d0407ac26"),
    ("mo1-7.5k", 0x0100, "543adb5348db81ea82a5c039451001ebd82735e3"),
    ("mo1-3.3m", 0x0100, "e21a0618c0f8e35ce26666b6850ac9c0d95d7971"),
)


def _write_tiles(ctx: Context, game: str, tiles) -> None:
    with ctx.header(f"{game}_tilemap.h") as f:
        print("// Dig Dug II tiles (d21_5.3b, ROMREGION_INVERT): 256 tiles 8x8 2bpp", file=f)
        print("// pixels LSB-first (blit: (pix>>2c)&3)", file=f)
        emit_compressed(f, f"{game}_tilemap", "unsigned short", "[8]", len(tiles), tile_rows(tiles))


def _write_sprites(ctx: Context, game: str, sprites) -> None:
    # unflipped: digdug2::blit_sprite flips at runtime
    with ctx.header(f"{game}_spritemap.h") as f:
        print("// Dig Dug II sprites (d21_6.3m+d21_7.3n interleaved): 256 sprites 16x16 4bpp", file=f)
        print("// [code][row*2+half]: nibbles LSB-first, [2r]=pixels 0-7, [2r+1]=pixels 8-15", file=f)
        emit_compressed(f, f"{game}_sprites", "uint32_t", "[32]", len(sprites), sprite_words(sprites))


def _write_colormaps(ctx: Context, game: str, pal_prom, char_lut, spr_lut) -> None:
    pal = [rgb565_swapped_packed(c) for c in pal_prom]  # 32 colors
    with ctx.header(f"{game}_cmap.h") as f:
        print("// Dig Dug II colormaps from d21-6.4c (chars, pen+0x10) and d21-7.5k", file=f)
        print("// (sprites, 16 groups x 16). Hardware transparency = lookup 0xF:", file=f)
        print("//  - tiles: opaque real color", file=f)
        print("//  - tiles_prio: 0x0000 where lookup==0xF (priority tiles over sprites)", file=f)
        print("//  - sprites: 0x0000 where lookup==0xF", file=f)
        tile_tables(f, game, pal, char_lut)
        flat = []
        for g in range(_SPRITE_GROUPS):
            for p in range(16):
                lut = spr_lut[g * 16 + p] & 0x0f
                flat.append(0 if lut == 0x0f else nudge(pal[lut]))
        emit_compressed(f, f"{game}_colormap_sprites", "unsigned short", "[16]", _SPRITE_GROUPS, flat)


def _convert(ctx: Context, game: str, files) -> None:
    roms = {}
    for name, size, sha1 in files:
        roms[name] = ctx.rom(name, sha1)
        assert len(roms[name]) == size, name
    main_hi, main_lo, sub, tiles, spr_even, spr_odd, pal, char_lut, spr_lut, wave = (n for n, _, _ in files)

    _write_tiles(ctx, game, inverted_tiles(roms[tiles]))
    # sprites: 16 bit interleaved stream (d21_6 = even bytes, d21_7 = odd)
    sprites = interleaved_sprites(roms[spr_even], roms[spr_odd], 0x8000, _NUM_SPRITES)
    assert len(sprites) == _NUM_SPRITES
    _write_sprites(ctx, game, sprites)
    _write_colormaps(ctx, game, roms[pal], roms[char_lut], roms[spr_lut])

    main_rom = roms[main_hi] + roms[main_lo]
    write_bytes(ctx, f"{game}_rom_main.h", f"{game}_rom_main", main_rom,
              f"{game} main M6809 ROM 0x8000-0xFFFF ({main_hi}+{main_lo})")
    write_bytes(ctx, f"{game}_rom_sub.h", f"{game}_rom_sub", roms[sub],
              f"{game} sound M6809 ROM 0xE000-0xFFFF ({sub})")
    # 256 bytes, low nibble = 8 waveforms x 32 samples, centered (-8..7)
    write_wsg(ctx, f"{game}_wavetable.h", f"{game}_wavetable",
              "Dig Dug II WSG 15XX waveforms (d21-3.3m): 8 waves x 32 samples", roms[wave])

    # resident (unpacked into the Arena) asset sizes
    sizes = {
        f"{game}_tilemap": 256 * 8 * 2,
        f"{game}_sprites": _NUM_SPRITES * 32 * 4,
        f"{game}_colormap_sprites": _SPRITE_GROUPS * 16 * 2,
        f"{game}_rom_main": len(main_rom),
        f"{game}_rom_sub": len(roms[sub]),
    }
    for name, size in sizes.items():
        ctx.log.info(f"  {name}: {size} bytes unpacked")
    log_sizes(ctx, sizes)

    ctx.log.info(f"{game} conversion finished.")


JOBS = (
    Job(name="digdug2", convert=partial(_convert, game="digdug2", files=_DIGDUG2), machine="digdug2",
        zips=("digdug2.zip",)),
    Job(name="motos", convert=partial(_convert, game="motos", files=_MOTOS), machine="motos",
        zips=("motos.zip",)),
)
