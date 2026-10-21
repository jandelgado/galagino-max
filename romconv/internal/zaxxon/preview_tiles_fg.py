#!/usr/bin/env python3
import os
import sys

sys.path.insert(0, os.path.join("..", "pyconv"))
sys.path.insert(0, os.path.join("..", ".."))
from gfxutil import load_file, render_tile_grid
from romconv.games.zaxxon import ZAXXON_FILES, decode_char_tiles

ROM_SET = os.path.join("..", "..", "..", "romszip", "zaxxon.zip")

OUTPUT_IMAGE_FILE = "zaxxon_fg_preview.png"
TILES_PER_ROW = 16
PREVIEW_PALETTE = {0: (0, 0, 0), 1: (255, 85, 85), 2: (85, 255, 85), 3: (255, 255, 85)}

def main():
    tx1 = load_file(ROM_SET, [ZAXXON_FILES["gfx_tx_1"][0]], ZAXXON_FILES["gfx_tx_1"][1])
    tx2 = load_file(ROM_SET, [ZAXXON_FILES["gfx_tx_2"][0]], ZAXXON_FILES["gfx_tx_2"][1])
    tiles = decode_char_tiles(tx1 + tx2)
    render_tile_grid(tiles, 8, 8, PREVIEW_PALETTE, TILES_PER_ROW, OUTPUT_IMAGE_FILE)

if __name__ == "__main__":
    main()
