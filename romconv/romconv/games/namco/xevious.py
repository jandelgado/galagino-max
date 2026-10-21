"""Xevious (Namco 1982) and Super Xevious.

Reference: MAME galaga.cpp (shared driver: ROM_START(xevious), gfx_layout, init_xevious()) and
xevious.cpp (video: palette, tile_info, planet map, sprites). The Namco 54xx ROM
(namco54_rom.h) comes from the namco54 job."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.games.namco.common import sprite_words, tile_rows, write_rom, write_wsg
from romconv.gfx.color import rgb565_swapped_scaled
from romconv.gfx.gfxutil import mame_decode, rot_galagino
from romconv.hw.namco import nudge

# file: (name, sha1)
_REQUIRED = (
    ("xvi_1.3p", "4882b25b0938a903f3a367455ba788a30759b5b0"),    # cpu1 @0x0000
    ("xvi_2.3m", "8adc60a5fcbca74092518dbc570ffff0f04c5b17"),    # cpu1 @0x1000
    ("xvi_3.2m", "c6a154858716e1f073b476824b183de20e06d093"),    # cpu1 @0x2000
    ("xvi_4.2l", "4b846de204d08651253d3a141677c8a31626af07"),    # cpu1 @0x3000
    ("xvi_5.3f", "15f1c005b9d806a384ab1f2240b9c580bfe83893"),    # cpu2 @0x0000
    ("xvi_6.3j", "6b79efee1a9642edb9f752101737132401248aed"),    # cpu2 @0x1000
    ("xvi_7.2c", "f8d1f8e019d8198308443c2e7e815d0d04b23d14"),    # cpu3 @0x0000
    ("xvi_12.3b", "9c3b61dfca2f84673a78f7f66e363777a8f47a59"),   # gfx1 fg char 1bpp
    ("xvi_13.3c", "32bc09be5ff8b52ee3a26e0ac3ebc2d4107badb7"),   # gfx2 bg tile plane0 (B0)
    ("xvi_14.3d", "fb9ffe5fc43e0213231267e98d605d43c15f61e8"),   # gfx2 bg tile plane1 (B1)
    ("xvi_15.4m", "19ddbd9805f77f38c9a9a1bb30dba6c720b8609f"),   # gfx3 sprite set1 planes0/1
    ("xvi_17.4p", "acff2bf5cde85a16cdc98a52cdea11f77fadf25a"),   # gfx3 sprite set2 planes0/1
    ("xvi_16.4n", "3bf380ef76c03822a042ecc73b5edd4543c268ce"),   # gfx3 sprite set3 planes0/1
    ("xvi_18.4r", "b5f830dd2cf25cf154308d2e640f0ecdcda5d8cd"),   # gfx3 sprite set1/2 plane2 (nibble packed)
    ("xvi_9.2a", "3106d1aacff06cf78371bd19967141072b32b7d7"),    # gfx4 planet-map 2A
    ("xvi_10.2b", "49064b25667ffcd81137cd5e800df4b78b182a46"),   # gfx4 planet-map 2B
    ("xvi_11.2c", "3f7eac12863697a98e1122111801606759e44b2a"),   # gfx4 planet-map 2C
    ("xvi-8.6a", "0dc1e63a47a4cb0ba75f6f1e0c15e408bb0ee2a1"),    # palette red
    ("xvi-9.6d", "63015e3c0874afc6b1ca032f1ffb8f90562c77c8"),    # palette green
    ("xvi-10.6e", "c94d5a5dd4d8a08d6d39c051a4a722581b903f45"),   # palette blue
    ("xvi-7.4h", "ec6626828c79350417d08b98e9631ad35edd4a41"),    # bg tiles lookup low
    ("xvi-6.4f", "a4bdf58c190ca16fc7b976c97f41087a61fdb8b8"),    # bg tiles lookup high
    ("xvi-4.3l", "87ddf0b9d723aabb422d6d416aa9ec6bc246bf34"),    # sprite lookup low
    ("xvi-5.3m", "776168a73d3b9f0ce05610acc8a623deae0a572b"),    # sprite lookup high
    ("xvi-2.7n", "816a0fafa0b084ac11ae1af70a5186539376fc2a"),    # namco WSG waveform
    ("xvi-1.5n", "0c4d0bee858b97632411c440bea6948a74759746"),    # namco timing (not used)
)

# layout MAME (absolute bit offsets in the buffer rebuilt like ROM_START + init_xevious())
# gfx1: 8x8x1bpp, 512 chars of 8 bytes
_CHAR_PLANES = (0,)
_CHAR_XOFFS = (0, 1, 2, 3, 4, 5, 6, 7)
_CHAR_YOFFS = tuple(y * 8 for y in range(8))
_CHAR_BITS_PER_TILE = 8 * 8
_CHAR_COUNT = 512

# gfx2: bgcharlayout 8x8x2bpp, RGN_FRAC(1,2), planes={0,half}; B0 + B1 = 0x2000 bytes
_BG_BITS_PER_TILE = 8 * 8
_BG_COUNT = 512  # 0x1000 (half of the region) / 8 bytes

# gfx3: spritelayout_xevious 16x16x3bpp, RGN_FRAC(1,2), planes={half+4, 0, 4}, same offsets as
# gaplus; the region is 0xa000 bytes after init_xevious()
_SPR_XOFFS = (0, 1, 2, 3, 8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
              16 * 8, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3, 24 * 8, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3)
_SPR_YOFFS = tuple(y * 8 for y in range(8)) + tuple(32 * 8 + y * 8 for y in range(8))
_SPR_BITS_PER_TILE = 64 * 8
_SPR_COUNT = 320  # 0xa000/2/64 sprites (set1: 128, set2: 128, set3: 64)


def _write_fg_tiles(ctx: Context, tiles) -> None:
    with ctx.header("xevious_fgtilemap.h") as f:
        print("// Xevious foreground text tiles (xvi_12.3b) — 512 char 8x8 1bpp", file=f)
        print("// pen0=transparent (mapped via colormap), direct index to fg_videoram", file=f)
        flat = []
        for t in tiles:
            for y in range(8):
                v = 0
                for x in range(8):
                    v |= t[y][x] << x
                flat.append(v)
        emit_compressed(f, "xevious_fgtilemap", "unsigned char", "[8]", len(tiles), flat)


def _write_bg_tiles(ctx: Context, tiles) -> None:
    with ctx.header("xevious_bgtilemap.h") as f:
        print("// Xevious background tiles (xvi_13.3c+xvi_14.3d) — 512 tile 8x8 2bpp", file=f)
        print("// no transparency: layer bg is OPAQUE (drawn first)", file=f)
        emit_compressed(f, "xevious_bgtilemap", "unsigned short", "[8]", len(tiles), tile_rows(tiles))


def _write_sprites(ctx: Context, sprites) -> None:
    # Non flippati, xevious::blit_sprite applica il flip a runtime.
    with ctx.header("xevious_spritemap.h") as f:
        print("// Xevious sprites (xvi_15.4m+xvi_17.4p+xvi_16.4n+xvi_18.4r, dopo", file=f)
        print("// init_xevious() unpack) — 320 sprite 16x16 3bpp (valori pixel 0-7),", file=f)
        print("// impacchettati a nibble (4 bit) come mappy/gaplus_spritemap.h.", file=f)
        emit_compressed(f, "xevious_sprites", "uint32_t", "[32]", len(sprites), sprite_words(sprites))


def _decode_palette(red_prom, green_prom, blue_prom) -> list[int]:
    # xevious_palette() (xevious.cpp): weighted resistors 0x0e/0x1f/0x43/0x8f
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


def _write_colormaps(ctx: Context, pal, bg_lut_lo, bg_lut_hi, spr_lut_lo, spr_lut_hi) -> None:
    # bg: groups of 4 pens (2bpp), sprite: groups of 8 pens (3bpp). Exact formulas from
    # xevious_palette() (xevious.cpp:70-99):
    #  bg:     pen = (lut_lo&0xf) | ((lut_hi&0xf)<<4)   (0-127, direct in pal[])
    #  sprite: c = (lut_lo&0xf)|((lut_hi&0xf)<<4); pen = (c&0x80)?(c&0x7f):0x80 (transparent)
    #  fg:     direct pen, odd -> pal[i>>1], even -> transparent (0x80)
    with ctx.header("xevious_cmap_bg.h") as f:
        print("// Xevious colormap sfondo, da xvi_7bpr.4h (lut low)/xvi_6bpr.4f (lut high)", file=f)
        print("// 128 gruppi x 4 pen (2bpp), layer OPACO -> nessuna trasparenza", file=f)
        flat = []
        for g in range(128):
            for p in range(4):
                idx = g * 4 + p
                lut = (bg_lut_lo[idx] & 0x0f) | ((bg_lut_hi[idx] & 0x0f) << 4)
                flat.append(nudge(pal[lut & 0x7f]))
        emit_compressed(f, "xevious_colormap_bg", "unsigned short", "[4]", 128, flat)

    with ctx.header("xevious_cmap_sprites.h") as f:
        print("// Xevious colormap sprite, da xvi_4bpr.3l (lut low)/xvi_5bpr.3m (lut high)", file=f)
        print("// 64 gruppi x 8 pen (3bpp). c&0x80==0 -> pen trasparente (valore 0).", file=f)
        flat = []
        for g in range(64):
            for p in range(8):
                idx = g * 8 + p
                c = (spr_lut_lo[idx] & 0x0f) | ((spr_lut_hi[idx] & 0x0f) << 4)
                flat.append(0 if (c & 0x80) == 0 else nudge(pal[c & 0x7f]))
        emit_compressed(f, "xevious_colormap_sprites", "unsigned short", "[8]", 64, flat)

    # fg colormap depends on the 6 bit color code of the tile (attr), exact formula in
    # get_fg_tile_info()/xevious_palette(): g = ((attr&3)<<4)|((attr&0x3c)>>2),
    # pen1 -> pal[g] direct, pen0 -> transparent
    with ctx.header("xevious_cmap_fg.h") as f:
        print("// Xevious colormap testo (xevious_palette, 'foreground characters map", file=f)
        print("// directly to a palette color', NESSUNA lut PROM). Per ogni color code", file=f)
        print("// a 6 bit (g = ((attr&3)<<4)|((attr&0x3c)>>2)) e pen 0/1: pen1 -> pal[g],", file=f)
        print("// pen0 -> trasparente (0).", file=f)
        print("const unsigned short xevious_colormap_fg[][2] = {", file=f)
        rows = []
        for g in range(64):
            rows.append("{" + f"0x0000,{hex(nudge(pal[g & 0x7f]))}" + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)


def _convert(ctx: Context) -> None:
    roms = {name: ctx.rom(name, sha1) for name, sha1 in _REQUIRED}

    # gfx1 (fg char): used as is
    fg_tiles = [rot_galagino(t) for t in
                mame_decode(roms["xvi_12.3b"], 8, 8, _CHAR_PLANES, _CHAR_XOFFS, _CHAR_YOFFS,
                            _CHAR_BITS_PER_TILE, _CHAR_COUNT)]
    _write_fg_tiles(ctx, fg_tiles)

    # gfx2 (bg tile): B0 + B1 concatenated
    bg_src = roms["xvi_13.3c"] + roms["xvi_14.3d"]
    bg_tiles = [rot_galagino(t) for t in
                mame_decode(bg_src, 8, 8, (0, 0x1000 * 8), _CHAR_XOFFS, _CHAR_YOFFS,
                            _BG_BITS_PER_TILE, _BG_COUNT)]
    _write_bg_tiles(ctx, bg_tiles)

    # gfx3 (sprites): replicates ROM_START(xevious) + init_xevious(), region of 0xa000 bytes:
    #  0x0000-0x1fff xvi_15.4m  (set1 planes0/1, nibble packed)
    #  0x2000-0x3fff xvi_17.4p  (set2 planes0/1)
    #  0x4000-0x4fff xvi_16.4n  (set3 planes0/1)
    #  0x5000-0x6fff xvi_18.4r  (set1+set2 plane2, nibble packed: high = plane2)
    #  0x7000-0x8fff init_xevious(): rom[0x7000+i] = rom[0x5000+i]>>4
    #  0x9000-0x9fff zero (set3 has no physical plane2)
    spr_full = bytearray(0xa000)
    spr_full[0x0000:0x2000] = roms["xvi_15.4m"]
    spr_full[0x2000:0x4000] = roms["xvi_17.4p"]
    spr_full[0x4000:0x5000] = roms["xvi_16.4n"]
    spr_full[0x5000:0x7000] = roms["xvi_18.4r"]
    for i in range(0x2000):
        spr_full[0x7000 + i] = (roms["xvi_18.4r"][i] >> 4) & 0xff

    half_bits = (0xa000 * 8) // 2  # RGN_FRAC(1,2)
    spr_planes = (half_bits + 4, 0, 4)
    sprites = [rot_galagino(s) for s in
               mame_decode(bytes(spr_full), 16, 16, spr_planes, _SPR_XOFFS, _SPR_YOFFS,
                           _SPR_BITS_PER_TILE, _SPR_COUNT)]
    _write_sprites(ctx, sprites)

    pal = _decode_palette(roms["xvi-8.6a"], roms["xvi-9.6d"], roms["xvi-10.6e"])
    _write_colormaps(ctx, pal, roms["xvi-7.4h"], roms["xvi-6.4f"], roms["xvi-4.3l"], roms["xvi-5.3m"])
    # only xvi_2bpr.7n is used (xvi_1bpr.5n = "timing - not used" in galaga.cpp)
    write_wsg(ctx, "xevious_wavetable.h", "xevious_wavetable",
              "Xevious WSG waveforms (xvi_2bpr.7n): 8 forme x 32 campioni", roms["xvi-2.7n"])

    # planet map (schematic 9B): raw flash data read at run time by xevious_bb_r()/xevious_bs_w()
    # (0xf000-0xffff), one blob like the real gfx4 region: 2a@0, 2b@0x1000, 2c@0x3000
    write_rom(ctx, "xevious_planetmap.h", "xevious_planetmap",
              roms["xvi_9.2a"] + roms["xvi_10.2b"] + roms["xvi_11.2c"],
              "Xevious planet-map lookup ROM (xvi_9.2a+xvi_10.2b+xvi_11.2c), 0x4000 byte: "
              "rom2a@0x0000 rom2b@0x1000 rom2c@0x3000, read via xevious_bb_r()")

    cpu1_rom = roms["xvi_1.3p"] + roms["xvi_2.3m"] + roms["xvi_3.2m"] + roms["xvi_4.2l"]
    write_rom(ctx, "xevious_rom_cpu1.h", "xevious_rom_cpu1", cpu1_rom,
              "Xevious CPU1 (main) Z80 ROM 0x0000-0x3FFF (xvi_1.3p+xvi_2.3m+xvi_3.2m+xvi_4.2l)")
    cpu2_rom = roms["xvi_5.3f"] + roms["xvi_6.3j"]
    write_rom(ctx, "xevious_rom_cpu2.h", "xevious_rom_cpu2", cpu2_rom,
              "Xevious CPU2 (motion) Z80 ROM 0x0000-0x1FFF (xvi_5.3f+xvi_6.3j)")
    write_rom(ctx, "xevious_rom_cpu3.h", "xevious_rom_cpu3", roms["xvi_7.2c"],
              "Xevious CPU3 (sound) Z80 ROM 0x0000-0x0FFF (xvi_7.2c)")
    ctx.log.info("Xevious conversion done.")


# Super Xevious: only the main and sub CPU ROMs differ (MAME galaga.cpp ROM_START(sxevious));
# every other asset comes from the xevious conversion
_SX_CPU1 = (
    ("cpu_3p.rom", "2c41303d8c74acb5840295a4b460a39a9a8e21bb"),
    ("cpu_3m.rom", "7169e7f3bd1e9cfae9671b89f2a45f56b968e1ff"),
    ("xv3_3.2m", "ecc39fb2c0065a36f20541747089b4e30dfb99b1"),
    ("xv3_4.2l", "0ca726f7f9528789f2a718df55e59406a283cdfa"),
)
_SX_CPU2 = (
    ("xv3_5.3f", "5831bb306bd650779207936bfd00f25864733abb"),
    ("xv3_6.3j", "5a020822387ab8c69214db961180760fa9853e6e"),
)


def _write_sx_rom(ctx: Context, symbol: str, parts, comment: str) -> int:
    data = b"".join(ctx.rom(name, sha1) for name, sha1 in parts)
    write_rom(ctx, symbol + ".h", symbol, data, comment)
    return len(data)


def _convert_sx(ctx: Context) -> None:
    total = _write_sx_rom(ctx, "sxevious_rom_cpu1", _SX_CPU1, "Super Xevious CPU1 (main) Z80 ROM 0x0000-0x3FFF")
    total += _write_sx_rom(ctx, "sxevious_rom_cpu2", _SX_CPU2, "Super Xevious CPU2 (motion) Z80 ROM 0x0000-0x1FFF")
    ctx.log.info(f"sxevious own resident assets: {total} B (rest shared with xevious)")


JOBS = (
    Job(name="xevious", convert=_convert, machine="xevious", zips=("xevious.zip",), deps=("namco54",)),
    Job(name="sxevious", convert=_convert_sx, machine="sxevious", zips=("sxevious.zip",), deps=("xevious",)),
)
