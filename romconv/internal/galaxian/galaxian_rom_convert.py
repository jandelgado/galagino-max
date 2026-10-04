#!/usr/bin/env python3
"""
Galaxian ROM converter for GALAGINO
Uses the same tile/sprite conversion as original galagino (spriteconv.py/tileconv.py).

ROM set (galmidw):
  Program: galmidw.u (2K), galmidw.v (2K), galmidw.w (2K), galmidw.y (2K), 7l (2K) = 10KB
  Graphics: 1h.bin (2K plane0), 1k.bin (2K plane1) = 4KB
  Color PROM: 6l.bpr (32 bytes)
"""

import os
import sys

sys.path.insert(0, os.path.join("..", "pyconv"))
from asset_emit import emit_compressed
from gfxutil import hex8, hex16, hex32
from galaxian_hw import parse_chr_2, dump_chr, convert_tiles, parse_sprite_galaxian, dump_sprite, convert_sprites
from convutil import fatal

ROM_SRC = os.path.normpath(os.path.join("..", "..", "roms"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "..", "source", "src", "machines", "galaxian"))

def load_file(name):
    path = os.path.join(ROM_SRC, name)
    if not os.path.exists(path):
        for alt in [name.lower(), name.upper()]:
            alt_path = os.path.join(ROM_SRC, alt)
            if os.path.exists(alt_path):
                path = alt_path
                break
    if not os.path.exists(path):
        fatal(f"File '{name}' non trovato in {os.path.abspath(ROM_SRC)}")
    with open(path, "rb") as f:
        return bytearray(f.read())

# ---- Color PROM -> RGB565 palette ----
def convert_colors(prom):
    rgb565 = []
    for i in range(32):
        b = prom[i]
        r = 0x21 * ((b >> 0) & 1) + 0x47 * ((b >> 1) & 1) + 0x97 * ((b >> 2) & 1)
        g = 0x21 * ((b >> 3) & 1) + 0x47 * ((b >> 4) & 1) + 0x97 * ((b >> 5) & 1)
        bl = 0x4F * ((b >> 6) & 1) + 0xA8 * ((b >> 7) & 1)
        r5 = (r >> 3) & 0x1F
        g6 = (g >> 2) & 0x3F
        b5 = (bl >> 3) & 0x1F
        val = (r5 << 11) | (g6 << 5) | b5
        # Byte-swap for ESP32 SPI display (matches Frogger/Pac-Man format)
        rgb565.append(((val & 0xFF) << 8) | ((val >> 8) & 0xFF))
    return rgb565

# ---- Write C header files ----
def write_rom(filename, name, data):
    with open(filename, 'w') as f:
        f.write("// Galaxian program ROM ({} bytes)\n".format(len(data)))
        emit_compressed(f, name, "unsigned char", "", len(data), list(data))
    print("Written: {} ({} bytes)".format(filename, len(data)))

def write_tilemap(filename, tiles):
    with open(filename, 'w') as f:
        f.write("// Galaxian tilemap: {} tiles, 8x8, 2bpp\n".format(len(tiles)))
        flat = [v for rows in tiles for v in rows]
        emit_compressed(f, "galaxian_tilemap", "unsigned short", "[8]", len(tiles), flat)
    print("Written: {} ({} tiles)".format(filename, len(tiles)))

def write_spritemap(filename, all_orientations):
    num_sprites = len(all_orientations[0])
    with open(filename, 'w') as f:
        f.write("// Galaxian spritemap: {} sprites, 16x16, 2bpp, 4 orientations\n".format(num_sprites))
        flat = [v for orientation in all_orientations for rows in orientation for v in rows]
        emit_compressed(f, "galaxian_spritemap", "uint32_t", "[%d][16]" % num_sprites, 4, flat)
    print("Written: {} ({} sprites x 4 orientations)".format(filename, num_sprites))

def write_colormap(filename, rgb565):
    with open(filename, 'w') as f:
        f.write("// Galaxian colormap: 8 palettes x 4 colors, RGB565\n")
        f.write("const unsigned short galaxian_colormap[][4] = {\n")
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

    print(f"Caricamento ROM da: {os.path.abspath(ROM_SRC)}")
    print(f"Destinazione file: {os.path.abspath(OUT_DIR)}")

    # Carica tutti i file
    rom_u = load_file("galmidw.u")
    rom_v = load_file("galmidw.v")
    rom_w = load_file("galmidw.w")
    rom_y = load_file("galmidw.y")
    rom_7l = load_file("7l")
    gfx_1h = load_file("1h.bin")
    gfx_1k = load_file("1k.bin")
    prom_6l = load_file("6l.bpr")

    # Verifica che tutti i file siano stati caricati
    files_ok = all(v is not None for v in [rom_u, rom_v, rom_w, rom_y, rom_7l, gfx_1h, gfx_1k, prom_6l])
    if not files_ok:
        fatal("Non tutti i file sono stati caricati correttamente.")

    print("Tutte le ROM caricate correttamente.")

    # Combine program ROM (pad to 16KB)
    program = rom_u + rom_v + rom_w + rom_y + rom_7l
    program += bytearray([0xFF] * (0x4000 - len(program)))

    write_rom(os.path.join(OUT_DIR, "galaxian_rom.h"), "galaxian_rom", program)

    # Convert tiles and sprites using original galagino algorithms
    tiles = convert_tiles(gfx_1h, gfx_1k)
    write_tilemap(os.path.join(OUT_DIR, "galaxian_tilemap.h"), tiles)

    sprites = convert_sprites(gfx_1h, gfx_1k)
    write_spritemap(os.path.join(OUT_DIR, "galaxian_spritemap.h"), sprites)

    rgb565 = convert_colors(prom_6l)
    write_colormap(os.path.join(OUT_DIR, "galaxian_cmap.h"), rgb565)

    print("\n--- OPERAZIONE COMPLETATA ---")
    print(f"Tutti i file sono stati generati in: {os.path.abspath(OUT_DIR)}")

if __name__ == "__main__":
    main()