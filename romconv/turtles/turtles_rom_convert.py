#!/usr/bin/env python3
"""
Turtles ROM converter for GALAGINO

ROM set (turtles) MAME:
  Main CPU:  5x 0x1000  turt_vid.2c/2e/2f/2h/2j
  Audio CPU: 2x 0x1000  turt_snd.5c/5d
  gfx:       2x 0x0800  turt_vid.5h/5f
  prom:      1x 0x0020  turtles.clr
"""

import os
import sys
import zipfile
import hashlib

sys.dont_write_bytecode = True

sys.path.insert(0, os.path.join("..", "pyconv"))
from romdata_emit import emit_compressed
from gfxutil import hex8, hex16, hex32
from galaxian_hw import parse_chr_2, dump_chr, convert_tiles, parse_sprite_galaxian, dump_sprite, convert_sprites
from convutil import fatal

ROM_SET = os.path.normpath(os.path.join("..", "..", "romszip", "turtles.zip"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "source", "src", "machines", "turtles"))

def load_file(names, sha1):
    for name in names:
        with zipfile.ZipFile(ROM_SET) as z:
            if name in z.namelist():
                with z.open(name, 'r') as f:
                    rom = bytearray(f.read())
                    digest = hashlib.sha1(rom).hexdigest()
                    if sha1 != digest:
                        fatal(f"bad hash for {name}: expected {sha1}, got {digest}")
                    return rom
    fatal(f"None of {names} found in {os.path.abspath(ROM_SET)}")

def write_rom(filename, name, data):
    with open(filename, 'w') as f:
        f.write("// Turtles program ROM ({} bytes)\n".format(len(data)))
        emit_compressed(f, name, "unsigned char", "", len(data), list(data))
    print("Wrote: {} ({} bytes)".format(os.path.abspath(filename), len(data)))

def write_tilemap(filename, tiles):
    with open(filename, 'w') as f:
        f.write("// Turtles tilemap: {} tiles, 8x8, 2bpp\n".format(len(tiles)))
        flat = [v for rows in tiles for v in rows]
        emit_compressed(f, "turtles_tilemap", "unsigned short", "[8]", len(tiles), flat)
    print("Wrote: {} ({} tiles)".format(os.path.abspath(filename), len(tiles)))

def write_spritemap(filename, all_orientations):
    num_sprites = len(all_orientations[0])
    with open(filename, 'w') as f:
        f.write("// Turtles spritemap: {} sprites, 16x16, 2bpp, 4 orientations\n".format(num_sprites))
        flat = [v for orientation in all_orientations for rows in orientation for v in rows]
        emit_compressed(f, "turtles_spritemap", "uint32_t", "[%d][16]" % num_sprites, 4, flat)
    print("Wrote: {} ({} sprites x 4 orientations)".format(os.path.abspath(filename), num_sprites))

def write_colormap(filename, rgb565):
    with open(filename, 'w') as f:
        f.write("// Turtles colormap: 8 palettes x 4 colors, RGB565\n")
        f.write("const unsigned short turtles_colormap[][4] = {\n")
        for pal in range(8):
            colors = rgb565[pal*4 : pal*4+4]
            f.write("  { " + ", ".join(hex16(c) for c in colors) + " }")
            if pal < 7: f.write(",")
            f.write("  // palette {}\n".format(pal))
        f.write("};\n")
    print("Wrote: {} (8 palettes)".format(os.path.abspath(filename)))

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
        rgb565.append(((val & 0xFF) << 8) | ((val >> 8) & 0xFF))
    return rgb565

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    print(f"Load ROM from: {os.path.abspath(ROM_SET)}")
    print(f"Target files:  {os.path.abspath(OUT_DIR)}")

    rom_2c = load_file(["turt_vid.2c"], "3ca89800fda7a7e61f54d71d5302908be2706def")
    rom_2e = load_file(["turt_vid.2e"], "af74602bf2454eb8f3b9bb5c425e2476feeecd69")
    rom_2f = load_file(["turt_vid.2f"], "2af9383e5a289c2d7fbe6cf5e5b1519c352afbab")
    rom_2h = load_file(["turt_vid.2h"], "3dcdf5dc601c875fc9d8b9a46e3ef588e7478e0d")
    rom_2j = load_file(["turt_vid.2j"], "bb1e91b2e6d4b5a861bf37907ef6b198328d8d83")

    rom_5c = load_file(["turt_snd.5c"], "5621f336e9be8acf986a34bbb8855ed5d45c28ef")
    rom_5d = load_file(["turt_snd.5d"], "8a49c55feba094b07380615cf0b6f0878c25a260")

    rom_5h = load_file(["turt_vid.5h"], "bc3f52cf6c6e19dfd2dacd1e8c9128f437e995fc")
    rom_5f = load_file(["turt_vid.5f"], "dee51d77be262a2944488e381541c10a2b6e5d83")

    prom_clr = load_file(["turtles.clr"], "09fd795170d7d30f101d579f57553da5ff3800ab")

    if not all(v is not None for v in [rom_2c, rom_2e, rom_2f, rom_2h, rom_2j,
                                        rom_5c, rom_5d, rom_5h, rom_5f, prom_clr]):
        fatal("Not all files loaded")

    main_cpu = rom_2c + rom_2e + rom_2f + rom_2h + rom_2j  # 5 x 4KB = 20KB
    write_rom(os.path.join(OUT_DIR, "turtles_main_rom.h"), "turtles_main_rom", main_cpu)

    audio_cpu = rom_5c + rom_5d  # 2 x 4KB = 8KB
    write_rom(os.path.join(OUT_DIR, "turtles_audio_rom.h"), "turtles_audio_rom", audio_cpu)

    tiles = convert_tiles(rom_5h, rom_5f)
    write_tilemap(os.path.join(OUT_DIR, "turtles_tilemap.h"), tiles)

    sprites = convert_sprites(rom_5h, rom_5f)
    write_spritemap(os.path.join(OUT_DIR, "turtles_spritemap.h"), sprites)

    rgb565 = convert_colors(prom_clr)
    write_colormap(os.path.join(OUT_DIR, "turtles_cmap.h"), rgb565)

    print("\n--- Complete ---")
    print(f"All files generated in: {os.path.abspath(OUT_DIR)}")

if __name__ == "__main__":
    main()
