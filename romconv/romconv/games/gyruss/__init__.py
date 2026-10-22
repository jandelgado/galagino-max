"""
Convert Gyruss (gyrussk) raw ROM dumps to galagino .h header files.

Reads the ROM files of gyruss.zip and generates, in source/src/machines/gyruss/:
  - gyruss_rom_main.h    (main Z80 CPU ROM, 24KB)
  - gyruss_rom_sub.h     (M6809 sub-CPU ROM, 8KB, Konami-1 encrypted)
  - gyruss_rom_audio.h   (audio Z80 ROM, 16KB)
  - gyruss_tilemap.h     (decoded 8x8 tiles from gyrussk.4)
  - gyruss_spritemap.h   (decoded 8x16 sprites from gyrussk.5/6/7/8)
  - gyruss_palette.h     (RGB565 palette + color lookup tables)
"""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.gfx.color import prom_rgb_332, rgb565_swapped
from romconv.gfx.gfxutil import column_words, mame_decode, pack_pens, step


# ============================================================
# CPU ROM assembly
# ============================================================

def _assemble_main_rom(ctx):
    """Assemble main Z80 CPU ROM (24KB: 0x0000-0x5FFF)."""
    rom = bytearray(0x6000)
    rom[0x0000:0x2000] = ctx.rom("gyrussk.1")
    rom[0x2000:0x4000] = ctx.rom("gyrussk.2")
    rom[0x4000:0x6000] = ctx.rom("gyrussk.3")
    return rom


def _assemble_sub_rom(ctx):
    """Assemble M6809 sub-CPU ROM (8KB: 0xE000-0xFFFF)."""
    return ctx.rom("gyrussk.9")  # 8KB, mapped at 0xE000-0xFFFF


def _assemble_audio_rom(ctx):
    """Assemble audio Z80 ROM (16KB: 0x0000-0x3FFF)."""
    rom = bytearray(0x4000)
    rom[0x0000:0x2000] = ctx.rom("gyrussk.1a")
    rom[0x2000:0x4000] = ctx.rom("gyrussk.2a")
    return rom


def _write_i8039_rom_h(ctx, rom):
    """Write i8039 sample MCU ROM header (drums, 4KB from gyrussk.3a)."""
    with ctx.header("gyruss_rom_i8039.h") as f:
        f.write("// Gyruss i8039 sample MCU ROM (4KB: 0x000-0xFFF)\n")
        f.write("// Generated from gyrussk.3a — drums/percussioni via DAC su P1\n\n")
        emit_compressed(f, "gyruss_rom_i8039", "unsigned char", "", len(rom), list(rom))


# ============================================================
# Tiles and sprites (MAME gyruss.cpp gfx layouts)
# ============================================================

def _decode_tiles(gfx_data):
    """512 tiles (8x8, 2bpp) from gyrussk.4, as uint16 words per native x
    column (ROT90). MAME charlayout has planes {4, 0}: decoded here as {0, 4},
    the char colormap swaps pens 1 and 2 back."""
    tiles = mame_decode(gfx_data, 8, 8, [0, 4], [0, 1, 2, 3, 64, 65, 66, 67], step(0, 8, 8), 128,
                        len(gfx_data) // 16)
    return column_words(tiles, 2)


def _decode_sprites(gfx_data):
    """512 sprites (8x16, 4bpp, canonical orientation) as one uint32 per row,
    first pen in the lowest nibble. Two MAME gfx elements interleave in the
    region: codes 0-255 start at byte 0x00, codes 256-511 at 0x10. X/Y flip is
    applied at render time (gyruss::blit_sprite)."""
    half = len(gfx_data) * 4  # RGN_FRAC(1,2) in bits
    yoffs = step(0, 8, 8) + step(256, 8, 8)
    sprites = []
    for base in (0x00, 0x10):
        sprites += mame_decode(gfx_data, 8, 16, [half + 4, half, 4, 0], [0, 1, 2, 3, 64, 65, 66, 67], yoffs,
                               64 * 8, 256, base * 8)
    return [pack_pens(row, 4) for s in sprites for row in s]


# ============================================================
# Palette and color lookup tables from PROMs
# ============================================================

def _generate_palette_and_colormaps(pr3, pr1, pr2):
    """Generate palette and color lookup tables."""
    # Generate 32 base colors from palette PROM
    palette_rgb = [prom_rgb_332(pr3[i]) for i in range(32)]

    # Palette as RGB565
    palette_565 = [rgb565_swapped(r, g, b) for r, g, b in palette_rgb]

    # Sprite color lookup: 16 groups x 16 colors
    sprite_colormap = []
    for group in range(16):
        colors = []
        for px in range(16):
            idx = group * 16 + px
            pal_idx = pr1[idx] & 0x0F
            r, g, b = palette_rgb[pal_idx]
            if px == 0:
                colors.append(0)  # transparent
            else:
                colors.append(rgb565_swapped(r, g, b))
        sprite_colormap.append(colors)

    # Character color lookup: pr2 maps to palette entries with +0x10 offset
    char_colormap = []
    for group in range(16):
        colors = []
        for px in range(4):
            c = px
            if px == 1:
                c = 2
            elif px == 2:
                c = 1
            idx = group * 4 + c
            pal_idx = (pr2[idx] & 0x0F) + 0x10
            if pal_idx >= 32:
                pal_idx = 0
            r, g, b = palette_rgb[pal_idx]
            if px == 0:
                colors.append(0)  # transparent
            else:
                colors.append(rgb565_swapped(r, g, b))
        char_colormap.append(colors)

    return palette_565, sprite_colormap, char_colormap


# ============================================================
# Output .h file writers
# ============================================================

def _write_main_rom_h(ctx, rom):
    """Write main Z80 ROM header."""
    with ctx.header("gyruss_rom_main.h") as f:
        f.write("// Gyruss main Z80 CPU ROM (24KB: 0x0000-0x5FFF)\n")
        f.write("// Generated from gyrussk.1 + gyrussk.2 + gyrussk.3\n\n")
        emit_compressed(f, "gyruss_rom_main", "unsigned char", "", len(rom), list(rom))


def _write_sub_rom_h(ctx, raw):
    """Write M6809 sub-CPU ROM header."""
    with ctx.header("gyruss_rom_sub.h") as f:
        f.write("// Gyruss M6809 sub-CPU ROM (8KB: mapped at 0xE000-0xFFFF)\n")
        f.write("// Generated from gyrussk.9\n")
        f.write("// Konami-1 encrypted, the M6809 core decrypts opcode fetches\n\n")
        emit_compressed(f, "gyruss_rom_sub_raw", "unsigned char", "", len(raw), list(raw))


def _write_audio_rom_h(ctx, rom):
    """Write audio Z80 ROM header."""
    with ctx.header("gyruss_rom_audio.h") as f:
        f.write("// Gyruss audio Z80 CPU ROM (16KB: 0x0000-0x3FFF)\n")
        f.write("// Generated from gyrussk.1a + gyrussk.2a\n\n")
        emit_compressed(f, "gyruss_rom_audio", "unsigned char", "", len(rom), list(rom))


def _write_tilemap_h(ctx, flat):
    """Write tilemap as C header, compressed."""
    with ctx.header("gyruss_tilemap.h") as f:
        f.write("// Gyruss character tiles (512 tiles, 8x8, 2bpp)\n")
        f.write("// Generated from gyrussk.4\n\n")
        emit_compressed(f, "gyruss_tilemap", "unsigned short", "[8]", len(flat) // 8, flat)


def _write_spritemap_h(ctx, flat):
    """Write spritemap as C header, compressed."""
    with ctx.header("gyruss_spritemap.h") as f:
        f.write("// Gyruss sprites (512 sprites, 8x16, 4bpp, canonical orientation)\n")
        f.write("// Generated from gyrussk.6 + gyrussk.5 + gyrussk.8 + gyrussk.7\n")
        f.write("// X/Y flip applied at render time -- see gyruss::blit_sprite\n\n")
        emit_compressed(f, "gyruss_sprites", "uint32_t", "[16]", 512, flat)


def _write_palette_h(ctx, palette_565, sprite_cmap, char_cmap):
    """Write palette and color lookup tables."""
    with ctx.header("gyruss_palette.h") as f:
        f.write("// Gyruss color data\n")
        f.write("// Generated from gyrussk.pr1 + gyrussk.pr2 + gyrussk.pr3\n\n")

        # Master palette (32 colors)
        f.write("// Master palette (32 entries, RGB565 byte-swapped)\n")
        f.write("const unsigned short gyruss_palette[] = {\n  ")
        f.write(",".join("0x{:04x}".format(c) for c in palette_565))
        f.write("\n};\n\n")

        # Sprite colormap (16 groups x 16 colors)
        f.write("// Sprite color lookup (16 groups x 16 colors, RGB565)\n")
        f.write("const unsigned short gyruss_sprite_colormap[][16] = {\n")
        for i, colors in enumerate(sprite_cmap):
            f.write("  { ")
            f.write(",".join("0x{:04x}".format(c) for c in colors))
            f.write(" }")
            if i < len(sprite_cmap) - 1:
                f.write(",")
            f.write("\n")
        f.write("};\n\n")

        # Character colormap (16 groups x 4 colors)
        f.write("// Character color lookup (16 groups x 4 colors, RGB565)\n")
        f.write("const unsigned short gyruss_char_colormap[][4] = {\n")
        for i, colors in enumerate(char_cmap):
            f.write("  { ")
            f.write(",".join("0x{:04x}".format(c) for c in colors))
            f.write(" }")
            if i < len(char_cmap) - 1:
                f.write(",")
            f.write("\n")
        f.write("};\n")


# ============================================================
# Main
# ============================================================

def _convert(ctx: Context) -> None:
    ctx.log.info("Gyruss ROM Conversion Tool")

    # 1. Main Z80 CPU ROM
    ctx.log.info("Assembling main Z80 ROM (24KB)...")
    main_rom = _assemble_main_rom(ctx)
    _write_main_rom_h(ctx, main_rom)
    ctx.log.info(f"  Written: gyruss_rom_main.h ({len(main_rom)} bytes)")

    # 2. M6809 sub-CPU ROM
    ctx.log.info("Assembling M6809 sub-CPU ROM (8KB)...")
    sub_raw = _assemble_sub_rom(ctx)
    _write_sub_rom_h(ctx, sub_raw)
    ctx.log.info(f"  Written: gyruss_rom_sub.h ({len(sub_raw)} bytes)")

    # 3. Audio Z80 ROM
    ctx.log.info("Assembling audio Z80 ROM (16KB)...")
    audio_rom = _assemble_audio_rom(ctx)
    _write_audio_rom_h(ctx, audio_rom)
    ctx.log.info(f"  Written: gyruss_rom_audio.h ({len(audio_rom)} bytes)")

    # 3b. i8039 sample MCU ROM (drums)
    ctx.log.info("Assembling i8039 sample MCU ROM (4KB)...")
    i8039_rom = ctx.rom("gyrussk.3a")
    _write_i8039_rom_h(ctx, i8039_rom)
    ctx.log.info(f"  Written: gyruss_rom_i8039.h ({len(i8039_rom)} bytes)")

    # 4. Tiles from gyrussk.4
    ctx.log.info("=== GENERAZIONE TILEMAP ===")
    tiles = _decode_tiles(ctx.rom("gyrussk.4"))
    _write_tilemap_h(ctx, tiles)
    ctx.log.info(f"  File finale: gyruss_tilemap.h ({len(tiles) // 8} tiles)")

    # 5. Sprites from gyrussk.6+5 (region 0) + gyrussk.8+7 (region 1)
    ctx.log.info("=== GENERAZIONE SPRITEMAP ===")
    ctx.log.info("Decoding sprites (512 x 8x16 x 4bpp)...")
    sprite_data = bytearray(0x8000)
    sprite_data[0x0000:0x2000] = ctx.rom("gyrussk.6")
    sprite_data[0x2000:0x4000] = ctx.rom("gyrussk.5")
    sprite_data[0x4000:0x6000] = ctx.rom("gyrussk.8")
    sprite_data[0x6000:0x8000] = ctx.rom("gyrussk.7")
    _write_spritemap_h(ctx, _decode_sprites(sprite_data))
    ctx.log.info("  File finale: gyruss_spritemap.h (512 sprites)")

    # 6. Palette and color maps from PROMs
    ctx.log.info("Generazione palette e color maps...")
    pr3 = ctx.rom("gyrussk.pr3")  # 32 bytes - palette
    pr1 = ctx.rom("gyrussk.pr1")  # 256 bytes - sprite lookup
    pr2 = ctx.rom("gyrussk.pr2")  # 256 bytes - char lookup
    palette_565, sprite_cmap, char_cmap = _generate_palette_and_colormaps(pr3, pr1, pr2)
    _write_palette_h(ctx, palette_565, sprite_cmap, char_cmap)
    ctx.log.info("  Written: gyruss_palette.h (32 palette + 16x16 sprite + 16x4 char colors)")

    ctx.log.info("Conversion complete!")
    ctx.log.info(f"  Main ROM:    {len(main_rom):6d} bytes")
    ctx.log.info(f"  Sub ROM:     {len(sub_raw):6d} bytes")
    ctx.log.info(f"  Audio ROM:   {len(audio_rom):6d} bytes")
    ctx.log.info(f"  Tiles:       {len(tiles) // 8:6d} (8x8, 2bpp)")
    ctx.log.info("  Sprites:     512 (8x16, 4bpp)")
    ctx.log.info("  Palette:     32 base colors + lookup tables")


JOB = Job(name="gyruss", convert=_convert, machine="gyruss", zips=("gyruss.zip",))
