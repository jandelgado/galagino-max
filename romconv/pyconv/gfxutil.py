import os
import zipfile
import hashlib

from convutil import fatal

# -------------------------------------------------------------------
# load file from zipfile and check hash
# -------------------------------------------------------------------
def load_file(rom_set, names, sha1):
  for name in names:
    with zipfile.ZipFile(rom_set) as z:
      if name in z.namelist():
        with z.open(name, 'r') as file:
          rom = bytearray(file.read())
          check_file(name, rom, sha1)
          return rom
  for name in names:
    fatal(f"File '{name}' not found in {os.path.abspath(rom_set)}")

def check_file(name, b, h):
  if h is None:
    return True
  digest = hashlib.sha1(b).hexdigest()
  if h != digest:
    fatal(f"bad hash for {name} {h}!={digest}.")
  return True

# -------------------------------------------------------------------
# generic gfx decoder MAME (planes/xoffs/yoffs as absolute OFFSET BIT)
# -------------------------------------------------------------------
def mame_decode(data, width, height, planes, xoffs, yoffs, bits_per_tile, count, base_bit=0):
    tiles = []
    for t in range(count):
        base = base_bit + t * bits_per_tile
        tile = []
        for y in range(height):
            row = []
            for x in range(width):
                v = 0
                for p in planes:
                    off = base + yoffs[y] + xoffs[x] + p
                    bit = (data[off >> 3] >> (7 - (off & 7))) & 1
                    v = (v << 1) | bit
                row.append(v)
            tile.append(row)
        tiles.append(tile)
    return tiles

# -------------------------------------------------------------------
# 90 deg rotation for galagino's portrait framebuffer
# -------------------------------------------------------------------
def rot_galagino(tile):
    n = len(tile)
    return [[tile[n - 1 - x][y] for x in range(n)] for y in range(n)]

# -------------------------------------------------------------------
def get_bit(value, bit):
  return (value >> bit) & 1

def rgb888_to_rgb565_le(r, g, b):
  r, g, b = [max(0, min(255, c)) for c in (r, g, b)]
  val_be = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
  return ((val_be & 0x00FF) << 8) | ((val_be & 0xFF00) >> 8)

# -------------------------------------------------------------------
def hex8(v):
  return "0x{:02x}".format(v & 0xFF)

def hex16(v):
  return "0x{:04x}".format(v & 0xFFFF)

def hex32(v):
  return "0x{:08x}".format(v & 0xFFFFFFFF)
