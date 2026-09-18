#!/usr/bin/env python3
"""
Moon Cresta ROM converter for GALAGINO
Uses the same tile/sprite conversion as original galagino (spriteconv.py/tileconv.py).

ROM set (galmidw):
  Program: galmidw.u (2K), galmidw.v (2K), galmidw.w (2K), galmidw.y (2K), 7l (2K) = 10KB
  Graphics: 1h.bin (2K plane0), 1k.bin (2K plane1) = 4KB
  Color PROM: 6l.bpr (32 bytes)

ROM set (mooncrst):
  Program:    mc1 (2K), mc2 (2K), mc3 (2K), mc4 (2K), mc5.7r (2K), mc6.8d (2K), mc7.8e (2K), mc8 (2K) = 16KB
  Graphics:   mcs_b (2K), mcs_d (2K), mcs_a (2K), mcs_c (2K) = 8KB
  Color PROM: l06_prom.bin (32 bytes)

ROM set (mooncrsu / mooncrstu) - Moon Cresta (Nichibutsu, unencrypted)
  Program:    smc1f (2K), smc2f (2K), smc3f (2K), smc4f (2K), e5 (2K), bepr199 (2K), e7 (2K), smc8f (2K) = 16KB
  Graphics:   mcs_b (2K), mcs_d (2K), mcs_a (2K), mcs_c (2K) = 8KB
  Color PROM: l06_prom.bin (32 bytes)
"""

import os
import sys

sys.path.insert(0, os.path.join("..", "pyconv"))
from romdata_emit import emit_compressed
from gfxutil import hex8, hex16, hex32
from galaxian_hw import parse_chr_2, dump_chr, convert_tiles, parse_sprite_galaxian, dump_sprite, convert_sprites
from convutil import fatal

ROM_SRC = os.path.normpath(os.path.join("..", "roms"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "source", "src", "machines", "mooncresta"))

def bitswap(value, *positions):
  """Reorders the bits of an 8-bit byte based on target positions."""
  res = 0
  for i, pos in enumerate(reversed(positions)):
    if (value >> pos) & 1:
      res |= (1 << i)
  return res

def decrypt_moon_cresta(input_data):
  """Applies the Nichibutsu hardware decryption algorithm."""
  output_data = bytearray(len(input_data))
    
  for offs in range(len(input_data)):
    data = input_data[offs]
    res = data

    # 1. Conditional XOR operations
    if data & (1 << 1):  # If bit 1 is set
      res ^= 0x40      # XOR bit 6
    if data & (1 << 5):  # If bit 5 is set
      res ^= 0x04      # XOR bit 2

    # 2. Bit swap on EVEN memory addresses only
    if not (offs & 1):
      # Swaps bit 2 and bit 6
      res = bitswap(res, 7, 2, 5, 4, 3, 6, 1, 0)

    output_data[offs] = res
        
  return output_data

def load_file(name):
    path = os.path.join(ROM_SRC, name)
    if not os.path.exists(path):
        for alt in [name.lower(), name.upper()]:
            alt_path = os.path.join(ROM_SRC, alt)
            if os.path.exists(alt_path):
                path = alt_path
                break
    if not os.path.exists(path):
        fatal(f"File '{name}' not found in {os.path.abspath(ROM_SRC)}")
    with open(path, "rb") as f:
        return bytearray(f.read())

# ---- Color PROM -> RGB565 palette ----
def convert_colors(prom):
    rgb565 = []
    for i in range(32):
        bits = prom[i]
        r = 0x21 * ((bits >> 0) & 1) + 0x47 * ((bits >> 1) & 1) + 0x97 * ((bits >> 2) & 1)
        g = 0x21 * ((bits >> 3) & 1) + 0x47 * ((bits >> 4) & 1) + 0x97 * ((bits >> 5) & 1)
        b = 0x4F * ((bits >> 6) & 1) + 0xA8 * ((bits >> 7) & 1)
        r5 = (r >> 3) & 0x1F
        g6 = (g >> 2) & 0x3F
        b5 = (b >> 3) & 0x1F
        val = (r5 << 11) | (g6 << 5) | b5

        # PROM: 7 6 5 4 3 2 1 0
        #     : b b g g g r r r
        red   = ((bits >> 0) & 0x07)
        green = ((bits >> 3) & 0x07)
        blue  = ((bits >> 6) & 0x03)

        # pallete 0 - 00..03
        # pallete 1 - 04..07
        # pallete 2 - 08..11
        # pallete 3 - 12..15
        # pallete 4 - 16..19
        # pallete 5 - 20..23
        # pallete 6 - 24..27
        # pallete 7 - 28..31
        # patch pallete #5 - fixes ship III colors and credits line
        if i == 20: val = 0b0000000000000000 # transparent
        if i == 21: val = 0b1101011010000000 # yellow  - III 
        if i == 22: val = 0b0000011100000000 # green   - outline 
        if i == 23: val = 0b0110101101011110 # magenta - body
        #                   rrrrrggggggbbbbb

        rgb565.append(((val & 0xFF) << 8) | ((val >> 8) & 0xFF))
    return rgb565

# ---- Write C header files ----
def write_rom(filename, name, data):
    with open(filename, 'w') as f:
        f.write("// Moon Cresta program ROM ({} bytes)\n".format(len(data)))
        emit_compressed(f, name, "unsigned char", "", len(data), list(data))
    print("Written: {} ({} bytes)".format(filename, len(data)))

def write_tilemap(filename, tiles):
    with open(filename, 'w') as f:
        f.write("// Moon Cresta tilemap: {} tiles, 8x8, 2bpp\n".format(len(tiles)))
        flat = [v for rows in tiles for v in rows]
        emit_compressed(f, "mooncresta_tilemap", "unsigned short", "[8]", len(tiles), flat)
    print("Written: {} ({} tiles)".format(filename, len(tiles)))

def write_spritemap(filename, all_orientations):
    num_sprites = len(all_orientations[0])
    with open(filename, 'w') as f:
        f.write("// Moon Cresta spritemap: {} sprites, 16x16, 2bpp, 4 orientations\n".format(num_sprites))
        flat = [v for orientation in all_orientations for rows in orientation for v in rows]
        emit_compressed(f, "mooncresta_spritemap", "uint32_t", "[%d][16]" % num_sprites, 4, flat)
    print("Written: {} ({} sprites x 4 orientations)".format(filename, num_sprites))

def write_colormap(filename, rgb565):
    with open(filename, 'w') as f:
        f.write("// Moon Cresta colormap: 8 palettes x 4 colors, RGB565\n")
        f.write("const unsigned short mooncresta_colormap[][4] = {\n")
        for pal in range(8):
            colors = rgb565[pal*4 : pal*4+4]
            f.write("  { " + ", ".join(hex16(c) for c in colors) + " }")
            if pal < 7:
                f.write(",")
            f.write("  // palette {}\n".format(pal))
        f.write("};\n")
    print("Written: {} (8 palettes)".format(filename))

# ---- Main ----
def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    print(f"Load ROM from: {os.path.abspath(ROM_SRC)}")
    print(f"Target file:   {os.path.abspath(OUT_DIR)}")

    # Load all files
    rom_mc1   = load_file("mc1")    # 0x0000 - mc1
    rom_mc2   = load_file("mc2")    # 0x0800 - mc2
    rom_mc3   = load_file("mc3")    # 0x1000 - mc3
    rom_mc4   = load_file("mc4")    # 0x1800 - mc4
    rom_mc5   = load_file("mc5.7r") # 0x2000 - mc5.7r
    rom_mc6   = load_file("mc6.8d") # 0x2800 - mc6.8d
    rom_mc7   = load_file("mc7.8e") # 0x3000 - mc7.8e
    rom_mc8   = load_file("mc8")    # 0x3800 - mc8
    gfx_mcs_b = load_file("mcs_b")
    gfx_mcs_d = load_file("mcs_d")
    gfx_mcs_a = load_file("mcs_a")
    gfx_mcs_c = load_file("mcs_c")
    prom_6l   = load_file("l06_prom.bin") # mmi6331.6l

    # Check that we have loaded all the files
    files_ok = all(v is not None for v in [rom_mc1, rom_mc2, rom_mc3, rom_mc4, rom_mc5, rom_mc6, rom_mc7, rom_mc8, 
                                           gfx_mcs_b, gfx_mcs_d, gfx_mcs_a, gfx_mcs_c, 
                                           prom_6l])
    if not files_ok:
        fatal("Not all files have been loaded")

    print("All files loaded.")

    # Combine program ROM (pad to 16KB)
    program = decrypt_moon_cresta(rom_mc1 + rom_mc2 + rom_mc3 + rom_mc4 + rom_mc5 + rom_mc6 + rom_mc7 + rom_mc8)
    if len(program) < 0x4000:
      program += bytearray([0xFF] * (0x4000 - len(program)))
    write_rom(os.path.join(OUT_DIR, "mooncresta_rom.h"), "mooncresta_rom", program)

    # Convert tiles and sprites using original galagino algorithms
    tiles = convert_tiles(gfx_mcs_b + gfx_mcs_d, gfx_mcs_a + gfx_mcs_c)
    write_tilemap(os.path.join(OUT_DIR, "mooncresta_tilemap.h"), tiles)

    sprites = convert_sprites(gfx_mcs_b + gfx_mcs_d, gfx_mcs_a + gfx_mcs_c)
    write_spritemap(os.path.join(OUT_DIR, "mooncresta_spritemap.h"), sprites)

    rgb565 = convert_colors(prom_6l)
    write_colormap(os.path.join(OUT_DIR, "mooncresta_cmap.h"), rgb565)

    print("\n--- Complete ---")
    print(f"All files generated in: {os.path.abspath(OUT_DIR)}")

if __name__ == "__main__":
    main()
