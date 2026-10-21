"""
Time Pilot ROM converter for GALAGINO
Konami 1982 - timeplt ROM set

ROM set:
  Program: tm1 (8K), tm2 (8K), tm3 (8K) = 24KB main CPU
  Sound:   tm7 (4K) = sound CPU
  Tiles:   tm6 (8K) = 8x8 chars, 2bpp
  Sprites: tm4 (8K) + tm5 (8K) = 16x16 sprites, 2bpp
  PROMs:   timeplt.b4 (32), timeplt.b5 (32) = palette
           timeplt.e9 (256) = sprite color lookup
           timeplt.e12 (256) = char color lookup
"""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.gfx.color import rgb565_swapped
from romconv.gfx.gfxutil import hex16


# ---- Color PROM -> RGB565 palette ----
# Time Pilot uses resistor-weighted DAC:
#   timeplt.b4: bits 7-3=BLUE(5 weights), bits 2-0=GREEN high 3 bits
#   timeplt.b5: bits 7-6=GREEN low 2 bits, bits 5-1=RED(5 weights), bit0=unused
# Resistor weights: 1.2K, 820, 560, 470, 390 ohm
# Normalized weights: 0x19, 0x24, 0x35, 0x40, 0x4D
def _convert_palette(prom_b4, prom_b5):
    weights = (0x19, 0x24, 0x35, 0x40, 0x4D)
    rgb565 = []
    for i in range(32):
        b4 = prom_b4[i]
        b5 = prom_b5[i]

        # RED from b5 bits 5-1 (5 resistors)
        r = (weights[0] * ((b5 >> 1) & 1) +
             weights[1] * ((b5 >> 2) & 1) +
             weights[2] * ((b5 >> 3) & 1) +
             weights[3] * ((b5 >> 4) & 1) +
             weights[4] * ((b5 >> 5) & 1))

        # GREEN from b5 bits 7-6 (low) + b4 bits 2-0 (high)
        g = (weights[0] * ((b5 >> 6) & 1) +
             weights[1] * ((b5 >> 7) & 1) +
             weights[2] * ((b4 >> 0) & 1) +
             weights[3] * ((b4 >> 1) & 1) +
             weights[4] * ((b4 >> 2) & 1))

        # BLUE from b4 bits 7-3 (5 resistors)
        b = (weights[0] * ((b4 >> 3) & 1) +
             weights[1] * ((b4 >> 4) & 1) +
             weights[2] * ((b4 >> 5) & 1) +
             weights[3] * ((b4 >> 6) & 1) +
             weights[4] * ((b4 >> 7) & 1))

        # (rgb565_swapped clamps to 255)
        # Convert to RGB565 byte-swapped for ESP32 SPI
        rgb565.append(rgb565_swapped(r, g, b))
    return rgb565

# ---- Build full palette using lookup PROMs ----
def _build_colormap(base_palette, prom_e12, prom_e9):
    """
    Char palettes: 32 palettes x 4 colors = 128 entries
    Sprite palettes: 64 palettes x 4 colors = 256 entries

    prom_e12: char lookup (256 bytes, but only 32*4=128 used)
    prom_e9:  sprite lookup (256 bytes, 64*4=256 used)

    Char colors use base_palette entries 16-31 (prom & 0x0F) + 0x10
    Sprite colors use base_palette entries 0-15 (prom & 0x0F)
    """
    char_colors = []
    for i in range(32 * 4):
        idx = (prom_e12[i] & 0x0F) + 0x10
        char_colors.append(base_palette[idx])

    sprite_colors = []
    for i in range(64 * 4):
        idx = prom_e9[i] & 0x0F
        sprite_colors.append(base_palette[idx])

    return char_colors, sprite_colors

# ---- Tile conversion (8x8, 2bpp) ----
# Konami charlayout (same as Gyruss):
#   planes: { 4, 0 }  (MSB-first: plane[0]=4 is MSB, plane[1]=0 is LSB)
#   xoffset: { 0,1,2,3, 64,65,66,67 }
#   yoffset: { 0*8, 1*8, ..., 7*8 }
#   charsize: 16 bytes per tile
#
# Pixel decode uses Gyruss-identical MSB-first approach (0x80 >> bit)
# PLUS 90 CW rotation because Time Pilot uses tileaddr (Gyruss does NOT)

def _convert_tiles(gfx_data):
    num_tiles = len(gfx_data) // 16  # 512 tiles
    tiles = []
    for tile_idx in range(num_tiles):
        base = tile_idx * 16
        # Step 1: decode landscape pixels using Gyruss-identical approach
        landscape = []
        for row in range(8):
            row_pixels = []
            for col in range(8):
                if col < 4:
                    byte = gfx_data[base + row]
                    mame_bit = col
                else:
                    byte = gfx_data[base + 8 + row]
                    mame_bit = col - 4
                p_lsb = 1 if (byte & (0x80 >> mame_bit)) else 0
                p_msb = 1 if (byte & (0x80 >> (mame_bit + 4))) else 0
                pixel = (p_msb << 1) | p_lsb
                row_pixels.append(pixel)
            landscape.append(row_pixels)

        # Step 2: rotate 90 CW for portrait (needed for tileaddr mapping)
        # portrait[py][px] = landscape[7-px][py]
        packed = []
        for py in range(8):
            row_val = 0
            for px in range(8):
                pixel = landscape[7 - px][py]
                row_val |= (pixel << (px * 2))
            packed.append(row_val)
        tiles.append(packed)
    return tiles

# ---- Sprite conversion (16x16, 2bpp) ----
# Gyruss-style MSB-first decode, NO rotation (landscape orientation).
# Transposed rendering at runtime handles the ROT90 display rotation.
# This preserves correct tiling for multi-sprite objects (clouds).

def _convert_sprites(gfx_data):
    num_sprites = len(gfx_data) // 64  # 256 sprites

    # Decode all sprites in landscape orientation (NO rotation)
    all_pixels = []
    for s in range(num_sprites):
        base = s * 64
        landscape = []
        for row in range(16):
            if row < 8:
                row_byte_base = row
            else:
                row_byte_base = 24 + row  # rows 8-15 at bytes 32-39
            row_pixels = []
            for col in range(16):
                col_group = col // 4
                bit_in_group = col % 4
                byte_off = row_byte_base + col_group * 8
                byte = gfx_data[base + byte_off]
                p_lsb = 1 if (byte & (0x80 >> bit_in_group)) else 0
                p_msb = 1 if (byte & (0x80 >> (bit_in_group + 4))) else 0
                pixel = (p_msb << 1) | p_lsb
                row_pixels.append(pixel)
            landscape.append(row_pixels)

        # NO rotation — keep landscape pixels as-is
        all_pixels.append(landscape)

    # Unflipped: timeplt::blit_sprite flips at draw time.
    sprites = []
    for sprite_pixels in all_pixels:
        packed = []
        for y in range(16):
            val = 0
            for x in range(16):
                val |= (sprite_pixels[y][x] << (x * 2))
            packed.append(val)
        sprites.append(packed)

    return sprites

# ---- Write C header files ----
def _write_rom(ctx, filename, name, data):
    with ctx.header(filename) as f:
        f.write("// Time Pilot program ROM ({} bytes)\n".format(len(data)))
        emit_compressed(f, name, "unsigned char", "", len(data), list(data))
    ctx.log.info("Written: {} ({} bytes)".format(filename, len(data)))

def _write_sound_rom(ctx, filename, name, data):
    with ctx.header(filename) as f:
        f.write("// Time Pilot sound ROM ({} bytes)\n".format(len(data)))
        emit_compressed(f, name, "unsigned char", "", len(data), list(data))
    ctx.log.info("Written: {} ({} bytes)".format(filename, len(data)))

def _write_tilemap(ctx, filename, tiles, char_colors):
    with ctx.header(filename) as f:
        f.write("// Time Pilot tilemap: {} tiles, 8x8, 2bpp\n".format(len(tiles)))
        flat = [v for rows in tiles for v in rows]
        emit_compressed(f, "timeplt_tilemap", "unsigned short", "[8]", len(tiles), flat)
        f.write("\n")

        # Char color palettes (32 palettes x 4 colors)
        f.write("// Time Pilot char color palettes: 32 palettes x 4 colors, RGB565 byte-swapped\n")
        f.write("const unsigned short timeplt_char_colormap[][4] = {\n")
        for pal in range(32):
            colors = char_colors[pal*4 : pal*4+4]
            f.write("  { " + ", ".join(hex16(c) for c in colors) + " }")
            if pal < 31:
                f.write(",")
            f.write("  // palette {}\n".format(pal))
        f.write("};\n")
    ctx.log.info("Written: {} ({} tiles, 32 char palettes)".format(filename, len(tiles)))

def _write_spritemap(ctx, filename, sprites, sprite_colors):
    num_sprites = len(sprites)
    with ctx.header(filename) as f:
        f.write("// Time Pilot spritemap: {} sprites, 16x16, 2bpp, unflipped\n".format(num_sprites))
        flat = [v for rows in sprites for v in rows]
        emit_compressed(f, "timeplt_spritemap", "uint32_t", "[16]", num_sprites, flat)
        f.write("\n")

        # Sprite color palettes (64 palettes x 4 colors)
        f.write("// Time Pilot sprite color palettes: 64 palettes x 4 colors, RGB565 byte-swapped\n")
        f.write("const unsigned short timeplt_sprite_colormap[][4] = {\n")
        for pal in range(64):
            colors = sprite_colors[pal*4 : pal*4+4]
            f.write("  { " + ", ".join(hex16(c) for c in colors) + " }")
            if pal < 63:
                f.write(",")
            f.write("  // palette {}\n".format(pal))
        f.write("};\n")
    ctx.log.info("Written: {} ({} sprites, 64 sprite palettes)".format(filename, num_sprites))

# ---- Main ----
def _convert(ctx: Context) -> None:
    # Carica tutti i file
    tm1 = ctx.rom("tm1")
    tm2 = ctx.rom("tm2")
    tm3 = ctx.rom("tm3")
    tm4 = ctx.rom("tm4")
    tm5 = ctx.rom("tm5")
    tm6 = ctx.rom("tm6")
    tm7 = ctx.rom("tm7")
    prom_b4 = ctx.rom("timeplt.b4")
    prom_b5 = ctx.rom("timeplt.b5")
    prom_e9 = ctx.rom("timeplt.e9")
    prom_e12 = ctx.rom("timeplt.e12")

    ctx.log.info("  tm1-tm3: {} + {} + {} = {} bytes (main CPU)".format(len(tm1), len(tm2), len(tm3), len(tm1)+len(tm2)+len(tm3)))
    ctx.log.info("  tm7: {} bytes (sound CPU)".format(len(tm7)))
    ctx.log.info("  tm6: {} bytes (tiles)".format(len(tm6)))
    ctx.log.info("  tm4+tm5: {} + {} = {} bytes (sprites)".format(len(tm4), len(tm5), len(tm4)+len(tm5)))

    # Combine program ROM (24KB, pad to 0x6000)
    program = tm1 + tm2 + tm3
    _write_rom(ctx, "timeplt_rom.h", "timeplt_rom", program)

    # Sound ROM
    _write_sound_rom(ctx, "timeplt_snd_rom.h", "timeplt_snd_rom", tm7)

    # Convert palette
    base_palette = _convert_palette(prom_b4, prom_b5)
    char_colors, sprite_colors = _build_colormap(base_palette, prom_e12, prom_e9)

    # Convert tiles (tm6 = 8KB = 512 tiles of 16 bytes each)
    tiles = _convert_tiles(tm6)
    _write_tilemap(ctx, "timeplt_tilemap.h", tiles, char_colors)

    # Convert sprites (tm4+tm5 = 16KB = 256 sprites of 64 bytes each)
    sprite_data = tm4 + tm5
    sprites = _convert_sprites(sprite_data)
    _write_spritemap(ctx, "timeplt_spritemap.h", sprites, sprite_colors)

    # NOTE: timeplt_dipswitches.h is maintained manually, not overwritten here

    ctx.log.info("--- OPERAZIONE COMPLETATA ---")


JOB = Job(name="timeplt", convert=_convert, machine="timeplt", zips=("timeplt.zip",))
