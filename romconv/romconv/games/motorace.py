"""
ROM converter for MotoRace USA (Irem, 1983) - travrusa.cpp MAME driver
Generates header files for GALAGONE ESP32-S3 emulator.

Hardware: Z80 @ 3.072MHz, Irem M52 sound, ROT270
  Tiles: 8x8, 3bpp planar, 1024 tiles
  Sprites: 16x16, 3bpp planar, 256 sprites
  Colors: 128 char + 16 sprite base colors, 128-entry sprite lookup

"""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.emit.write import module_path
from romconv.gfx.color import rgb565_swapped


def _bitswap16(val, *bits):
    result = 0
    for i, b in enumerate(bits):
        if val & (1 << b):
            result |= 1 << (15 - i)
    return result


def _bitswap8(val, *bits):
    result = 0
    for i, b in enumerate(bits):
        if val & (1 << b):
            result |= 1 << (7 - i)
    return result


def _decrypt_motorace(data):
    """MAME init_motorace: address and data line scrambling for first 8KB ROM"""
    buf = bytearray(data)
    rom = bytearray(0x2000)
    for A in range(0x2000):
        # Address bitswap: 15,14,13, 9,7,5,3,1, 12,10,8,6,4,2,0, 11
        j = _bitswap16(A, 15,14,13, 9,7,5,3,1, 12,10,8,6,4,2,0, 11)
        j &= 0x1FFF
        # Data bitswap: 2,7,4,1,6,3,0,5
        rom[j] = _bitswap8(buf[A], 2,7,4,1,6,3,0,5)
    return bytes(rom)


def _plane_pixels(p0, p1, p2, offset):
    """8 pixels of one byte per plane, MAME gfx order: pixel 0 = bit 7, p2 = pen bit 2."""
    return [((p0[offset] >> (7 - x)) & 1) | (((p1[offset] >> (7 - x)) & 1) << 1)
            | (((p2[offset] >> (7 - x)) & 1) << 2) for x in range(8)]


def _decode_tile_3bpp(p0, p1, p2, tile_idx):
    """One 8x8 tile (gfx_8x8x3_planar), native orientation: 8 rows of 8 pens."""
    return [_plane_pixels(p0, p1, p2, tile_idx * 8 + y) for y in range(8)]


def _decode_sprite_3bpp(p0, p1, p2, spr_idx):
    """One 16x16 sprite (MAME spritelayout: left 8 columns in bytes 0-15, right 8 in 16-31),
    native orientation: 16 rows of 16 pens."""
    base = spr_idx * 32
    return [_plane_pixels(p0, p1, p2, base + y) + _plane_pixels(p0, p1, p2, base + 16 + y)
            for y in range(16)]


def _rot270(rows):
    """Native pixels to screen orientation (MAME ROT270): screen row r, column c is native
    x = n - 1 - r, y = c."""
    n = len(rows)
    return [[rows[c][n - 1 - r] for c in range(n)] for r in range(n)]


def _pack_row(pixels):
    """Pack 8 pens into a uint32_t, pixel 0 at the top nibble."""
    val = 0
    for i, px in enumerate(pixels):
        val |= (px & 7) << ((7 - i) * 4)
    return val


def _prom_rgb(byte_val):
    """Colour PROM byte to RGB, characters and sprites alike (MAME 0.289 snapshots: sprite red
    bits 7, 6 = 255)."""
    r = 0x52 * ((byte_val >> 6) & 1) + 0xAD * ((byte_val >> 7) & 1)
    g = 0x21 * ((byte_val >> 3) & 1) + 0x47 * ((byte_val >> 4) & 1) + 0x97 * ((byte_val >> 5) & 1)
    b = 0x21 * ((byte_val >> 0) & 1) + 0x47 * ((byte_val >> 1) & 1) + 0x97 * ((byte_val >> 2) & 1)
    return r, g, b


def _convert(ctx: Context) -> None:
    def load(name):
        return ctx.rom(name)

    # ══════════════════════════════════════════════════════════════════
    # 1. CPU ROM — decrypt mr.cpu + concatenate mr1-mr3
    # ══════════════════════════════════════════════════════════════════
    ctx.log.info("Loading CPU ROMs...")
    cpu0 = _decrypt_motorace(load("mr.cpu"))
    cpu1 = load("mr1.3l")
    cpu2 = load("mr2.3k")
    cpu3 = load("mr3.3j")
    cpu_rom = cpu0 + cpu1 + cpu2 + cpu3  # 32KB total
    ctx.log.info(f"  CPU ROM: {len(cpu_rom)} bytes")

    # ══════════════════════════════════════════════════════════════════
    # 2. SOUND ROM — mr10.1a (4KB, loaded at offset 0x7000)
    # ══════════════════════════════════════════════════════════════════
    ctx.log.info("Loading Sound ROM...")
    snd_rom_data = load("mr10.1a")
    # Pad to 8KB with 0xFF before the data (it loads at 0x7000 in 8KB region)
    snd_rom = b'\xff' * 0x3000 + snd_rom_data
    ctx.log.info(f"  Sound ROM: {len(snd_rom_data)} bytes (padded to {len(snd_rom)})")

    # ══════════════════════════════════════════════════════════════════
    # 3. TILE GFX — mr7.3e, mr8.3c, mr9.3a (3 planes, 8KB each)
    #    Format: 8x8, 3bpp planar (gfx_8x8x3_planar)
    #    1024 tiles, each plane has 8 bytes per tile
    # ══════════════════════════════════════════════════════════════════
    ctx.log.info("Loading Tile ROMs...")
    tile_plane0 = load("mr7.3e")   # plane 0 (bit 0)
    tile_plane1 = load("mr8.3c")   # plane 1 (bit 1)
    tile_plane2 = load("mr9.3a")   # plane 2 (bit 2)
    num_tiles = len(tile_plane0) // 8  # 1024 tiles
    ctx.log.info(f"  Tiles: {num_tiles}")

    # Stored in screen orientation (ROT270), see motorace::draw_tiles
    tiles_packed = []
    for t in range(num_tiles):
        rows = _rot270(_decode_tile_3bpp(tile_plane0, tile_plane1, tile_plane2, t))
        tiles_packed.append([_pack_row(row) for row in rows])

    # ══════════════════════════════════════════════════════════════════
    # 4. SPRITE GFX — mr4.3n, mr5.3m, mr6.3k (3 planes, 8KB each)
    #    Format: 16x16, 3bpp planar
    #    MAME spritelayout: planes {2/3, 1/3, 0/3}, xoffsets {0-7, 128+0..128+7}, yoffsets {0*8..15*8}
    #    charincrement = 32*8 = 256 bits = 32 bytes per sprite per plane
    #    256 sprites total
    # ══════════════════════════════════════════════════════════════════
    ctx.log.info("Loading Sprite ROMs...")
    spr_plane0 = load("mr4.3n")   # RGN_FRAC(0,3) = LSB (bit 0)
    spr_plane1 = load("mr5.3m")   # RGN_FRAC(1,3) = mid (bit 1)
    spr_plane2 = load("mr6.3k")   # RGN_FRAC(2,3) = MSB (bit 2)
    num_sprites = len(spr_plane0) // 32  # 256 sprites
    ctx.log.info(f"  Sprites: {num_sprites}")

    # Store only the canonical (unflipped) orientation -- flip_x/flip_y are
    # mirrored at render time (motorace::draw_sprite), same convention as
    # every other machine in this codebase (e.g. 1942's tile_row_words).
    # Baking all 4 flip combinations here would quadruple the decompressed
    # footprint past Arena's budget (see asset.h / arena.h).
    # Rotated like the tiles (ROT270).
    sprites_packed = []  # [sprite_idx] = list of 16 (w0,w1) tuples
    for s in range(num_sprites):
        rows = _rot270(_decode_sprite_3bpp(spr_plane0, spr_plane1, spr_plane2, s))
        sprites_packed.append([(_pack_row(row[:8]), _pack_row(row[8:])) for row in rows])

    # ══════════════════════════════════════════════════════════════════
    # 5. COLOR PROMS
    # ══════════════════════════════════════════════════════════════════
    ctx.log.info("Loading Color PROMs...")
    char_prom = load("mmi6349.ij")    # 512 bytes, char palette (first 128 used)
    spr_prom = load("tbp18s.2")       # 32 bytes, sprite base palette (16 entries)
    spr_lookup = load("tbp24s10.3")   # 256 bytes, sprite color lookup table

    # Character colors: 128 indirect colors (indices 0-127)
    # Each PROM byte maps directly to a color
    char_colors_rgb = []
    for i in range(128):
        r, g, b = _prom_rgb(char_prom[i])
        char_colors_rgb.append(rgb565_swapped(r, g, b))

    # Sprite base colors: 16 entries from sprite PROM (at offset 0x200 in combined PROMs)
    spr_base_rgb = []
    for i in range(16):
        r, g, b = _prom_rgb(spr_prom[i])
        spr_base_rgb.append(rgb565_swapped(r, g, b))

    # Character colormap: 16 palettes x 8 colors
    # Characters use direct color mapping: palette * 8 + pixel_value → char_colors[palette*8+pixel]
    # In MAME: pen_indirect(i, i) for i < 0x80, so char pixel i → indirect color i
    # tile attr & 0x0F = palette (0-15), pixel 0-7 → color index = palette*8 + pixel
    char_colormap = []
    for pal in range(16):
        colors = []
        for px in range(8):
            idx = pal * 8 + px
            if idx < 128:
                colors.append(char_colors_rgb[idx])
            else:
                colors.append(0)
        char_colormap.append(colors)

    # Sprite colormap: 16 palettes x 8 colors
    # MAME: pen_indirect(0x80+i, (spr_lookup[i] & 0x0F) | 0x80) for sprite pens
    # Sprite pixel (0-7) with attr palette (0-15):
    #   lookup_index = palette * 8 + pixel (128 entries in lookup table, but only 0x80 used)
    #   Wait - the lookup table is 256 bytes (0x100) at offset 0x220
    #   For sprites: pen i (0x80 to 0xFF) → ctabentry = (lookup[i-0x80] & 0x0F) | 0x80
    #   So: pen = attr_palette * 8 + pixel_value (0-127 within sprite range)
    #   → indirect_color = (spr_lookup[pen] & 0x0F) | 0x80
    #   → spr_base_rgb[spr_lookup[pen] & 0x0F]
    spr_colormap = []
    for pal in range(16):
        colors = []
        for px in range(8):
            lookup_idx = pal * 8 + px
            if lookup_idx < len(spr_lookup):
                base_idx = spr_lookup[lookup_idx] & 0x0F
                colors.append(spr_base_rgb[base_idx])
            else:
                colors.append(0)
        spr_colormap.append(colors)

    # ══════════════════════════════════════════════════════════════════
    # 6. WRITE HEADER FILES
    # ══════════════════════════════════════════════════════════════════

    # ── motorace_rom.h ──
    ctx.log.info("Writing motorace_rom.h...")
    with ctx.header("motorace_rom.h") as f:
        f.write("// MotoRace USA - CPU ROM (32KB, first 8KB decrypted)\n")
        f.write(f"// Auto-generated by {module_path(__file__)}\n\n")
        emit_compressed(f, "motorace_rom", "unsigned char", "", len(cpu_rom), list(cpu_rom))

    # ── motorace_snd_rom.h ──
    ctx.log.info("Writing motorace_snd_rom.h...")
    with ctx.header("motorace_snd_rom.h") as f:
        f.write("// MotoRace USA - Sound ROM (4KB at offset 0x7000)\n")
        f.write(f"// Auto-generated by {module_path(__file__)}\n\n")
        emit_compressed(f, "motorace_snd_rom", "unsigned char", "", len(snd_rom), list(snd_rom))

    # ── motorace_tilemap.h ──
    ctx.log.info("Writing motorace_tilemap.h...")
    with ctx.header("motorace_tilemap.h") as f:
        f.write("// MotoRace USA - Tilemap: 1024 tiles, 8x8, 3bpp, rotated ROT270\n")
        f.write("// 4 bits per pixel packed in uint32_t (8 pixels per row)\n")
        f.write(f"// Auto-generated by {module_path(__file__)}\n\n")
        flat = [tiles_packed[t][r] for t in range(num_tiles) for r in range(8)]
        emit_compressed(f, "motorace_tilemap", "uint32_t", "[8]", num_tiles, flat)

    # ── motorace_spritemap.h ──
    ctx.log.info("Writing motorace_spritemap.h...")
    with ctx.header("motorace_spritemap.h") as f:
        f.write("// MotoRace USA - Spritemap: 256 sprites, 16x16, 3bpp, rotated ROT270\n")
        f.write("// (flip_x/flip_y mirrored at render time, see motorace::draw_sprite)\n")
        f.write("// Two uint32_ts per row (8+8 pixels), 4 bits per pixel\n")
        f.write(f"// Auto-generated by {module_path(__file__)}\n\n")
        flat = []
        for s in range(num_sprites):
            for r in range(16):
                w0, w1 = sprites_packed[s][r]
                flat.append(w0)
                flat.append(w1)
        emit_compressed(f, "motorace_spritemap", "uint32_t", "[32]", num_sprites, flat)

    # ── motorace_cmap.h ──
    ctx.log.info("Writing motorace_cmap.h...")
    with ctx.header("motorace_cmap.h") as f:
        f.write("// MotoRace USA - Color maps (RGB565 byte-swapped for ESP32 SPI)\n")
        f.write(f"// Auto-generated by {module_path(__file__)}\n\n")

        # Character colormap: 16 palettes x 8 colors
        f.write("// Character colormap: 16 palettes x 8 colors\n")
        f.write("const unsigned short motorace_char_cmap[16][8] PROGMEM = {\n")
        for pal in range(16):
            vals = [f"0x{char_colormap[pal][c]:04x}" for c in range(8)]
            f.write("  { " + ", ".join(vals) + " },\n")
        f.write("};\n\n")

        # Sprite colormap: 16 palettes x 8 colors
        f.write("// Sprite colormap: 16 palettes x 8 colors\n")
        f.write("const unsigned short motorace_spr_cmap[16][8] PROGMEM = {\n")
        for pal in range(16):
            vals = [f"0x{spr_colormap[pal][c]:04x}" for c in range(8)]
            f.write("  { " + ", ".join(vals) + " },\n")
        f.write("};\n")

    ctx.log.info("Done! motorace headers written")


JOB = Job(name="motorace", convert=_convert, machine="motorace", zips=("motorace.zip",))
