#!/usr/bin/env python3
"""
Super Cobra ROM converter for GALAGINO

ROM set (scobra) current MAME srcs:
  Main CPU:  8x 0x0800 s1.2d s2.2e s3.2f s4.2h s5.2j s6.2l s7.2m s8.2p
  Audio CPU: 3x 0x0800 ot1.5c ot2.5d ot3.5e
  gfx1:      2x 0x0800 c2.5f c1.5h
  prom:      1x 0x0020 c01s.6e

This should work with both sets and will check rom set against MAME SHA1 hashes.

"""

import os
import sys
import zipfile
import hashlib

sys.dont_write_bytecode = True

sys.path.insert(0, os.path.join("..", "pyconv"))
from asset_emit import emit_compressed
from gfxutil import hex8, hex16, hex32
from galaxian_hw import parse_chr_2, dump_chr, convert_tiles, parse_sprite_galaxian, dump_sprite, convert_sprites
from convutil import fatal

ROM_SET = os.path.normpath(os.path.join("..", "..", "..", "romszip", "scobra.zip"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "..", "source", "src", "machines", "supercobra"))

# -------------------------------------------------------------------
# load file from zipfile and check hash
# -------------------------------------------------------------------
# load file from zipfile and check hash
def load_file(names, sha1):
  for name in names:
    with zipfile.ZipFile(ROM_SET) as z:
      if name in z.namelist():
        with z.open(name, 'r') as file:
          rom = bytearray(file.read())
          check_file(name, rom, sha1)
          return rom
  for name in names:
    fatal(f"File '{name}' not found in {os.path.abspath(ROM_SET)}")

def check_file(name, b, h):
  digest = hashlib.sha1(b).hexdigest()
  if h != digest:
    fatal(f"bad hash for {name} {h}!={digest}.")
  return True

# -------------------------------------------------------------------
# ---- Write C header files ----
# -------------------------------------------------------------------
def write_rom(filename, name, data):
  with open(filename, 'w') as f:
    f.write("// Super Cobra program ROM ({} bytes)\n".format(len(data)))
    emit_compressed(f, name, "unsigned char", "", len(data), list(data))
  print("Wrote: {} ({} bytes)".format(os.path.abspath(filename), len(data)))

def write_tilemap(filename, tiles):
  with open(filename, 'w') as f:
    f.write("// Super Cobra tilemap: {} tiles, 8x8, 2bpp\n".format(len(tiles)))
    flat = [v for rows in tiles for v in rows]
    emit_compressed(f, "supercobra_tilemap", "unsigned short", "[8]", len(tiles), flat)
  print("Wrote: {} ({} tiles)".format(os.path.abspath(filename), len(tiles)))

def write_spritemap(filename, all_orientations):
  num_sprites = len(all_orientations[0])
  with open(filename, 'w') as f:
    f.write("// Super Cobra spritemap: {} sprites, 16x16, 2bpp, 4 orientations\n".format(num_sprites))
    flat = [v for orientation in all_orientations for rows in orientation for v in rows]
    emit_compressed(f, "supercobra_spritemap", "uint32_t", "[%d][16]" % num_sprites, 4, flat)
  print("Wrote: {} ({} sprites x 4 orientations)".format(os.path.abspath(filename), num_sprites))

def write_colormap(filename, rgb565):
  with open(filename, 'w') as f:
    f.write("// scobra colormap: 8 palettes x 4 colors, RGB565\n")
    f.write("const unsigned short supercobra_colormap[][4] = {\n")
    for pal in range(8):
      colors = rgb565[pal*4 : pal*4+4]
      f.write("  { " + ", ".join(hex16(c) for c in colors) + " }")
      if pal < 7:
        f.write(",")
      f.write("  // palette {}\n".format(pal))
    f.write("};\n")
  print("Wrote: {} (8 palettes)".format(os.path.abspath(filename)))

# -------------------------------------------------------------------
# ---- Color PROM -> RGB565 palette ----
# -------------------------------------------------------------------
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
    #   : b b g g g r r r
    red   = ((bits >> 0) & 0x07)
    green = ((bits >> 3) & 0x07)
    blue  = ((bits >> 6) & 0x03)
    rgb565.append(((val & 0xFF) << 8) | ((val >> 8) & 0xFF))
  return rgb565

# -------------------------------------------------------------------
# main()
# -------------------------------------------------------------------
def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    print(f"Load ROM from: {os.path.abspath(ROM_SET)}")
    print(f"Target files:  {os.path.abspath(OUT_DIR)}")

    # maincpu
    rom_2c = load_file(["epr1265.2c"], "8949298a04f8ba8a82d5d84a7b012a0e7cff11df")
    rom_2e = load_file(["2e"],         "281504ff364c3ddbf901c92729b139afd93b9785")
    rom_2f = load_file(["epr1267.2f"], "01775ad11dc23469649539ee8fb8a5800df031c6")
    rom_2h = load_file(["2h"],         "f5fff565ed3f6c5f277a4db53c9f569813fcec1d")
    rom_2j = load_file(["epr1269.2j"], "2add8270352d6596052d3ff22c891ceccaa92071")
    rom_2l = load_file(["2l"],         "5681771ed51d504bdcc2999fcbf926a30b137828")

    # audiocpu
    rom_5c = load_file(["5c"], "5eab4505beb69a5bdd88b23db60e1193371250cf")
    rom_5d = load_file(["5d"], "2b0784c4d05c466e0b7648f16e14f34393d792c3")
    rom_5e = load_file(["5e"], "ec79a73e4a2d7373454b227dd7eff255f1cc60cc")

    # gfx1
    rom_5h = load_file(["epr1274.5h"], "7b439bb74d5ecc792e0ca8964bcca8c6b7a51262")
    rom_5f = load_file(["epr1273.5f"], "9de0e94932e91dc34aea7c81880bde6a486d103b")

    # prom
    prom_6e = load_file(["82s123.6e"], "d11ac5e4a6057301ea2a9cbb404c2b978eb4c1dc")
    
    files_ok = all(v is not None for v in [ rom_2c, rom_2e, rom_2f, rom_2h, rom_2j, rom_2l,
                                            rom_5c, rom_5d, rom_5e,
                                            rom_5h, rom_5f,
                                            prom_6e])
    if not files_ok:
      fatal("Not all files have been loaded")

    # convert main cpu rom - 0x4000 (rom_size == 0x3000)
    main_cpu = rom_2c + rom_2e + rom_2f + rom_2h + rom_2j + rom_2l
    if len(main_cpu) < 0x4000:
      main_cpu += bytearray([0xFF] * (0x4000 - len(main_cpu)))
    write_rom(os.path.join(OUT_DIR, "supercobra_main_rom.h"), "supercobra_main_rom", main_cpu)
       
    # convert audio cpu rom - 0x2000
    audio_cpu = rom_5c + rom_5d + rom_5e
    if len(audio_cpu) < 0x2000:
      audio_cpu += bytearray([0xFF] * (0x2000 - len(audio_cpu)))
    write_rom(os.path.join(OUT_DIR, "supercobra_audio_rom.h"), "supercobra_audio_rom", audio_cpu)

    # Convert tiles and sprites using original galagino algorithms
    tiles = convert_tiles(rom_5h, rom_5f)
    write_tilemap(os.path.join(OUT_DIR, "supercobra_tilemap.h"), tiles)

    sprites = convert_sprites(rom_5h, rom_5f)
    write_spritemap(os.path.join(OUT_DIR, "supercobra_spritemap.h"), sprites)

    rgb565 = convert_colors(prom_6e)
    write_colormap(os.path.join(OUT_DIR, "supercobra_cmap.h"), rgb565)

    print("\n--- Complete ---")
    print(f"All files generated in: {os.path.abspath(OUT_DIR)}")
     
if __name__ == "__main__":
    main()
