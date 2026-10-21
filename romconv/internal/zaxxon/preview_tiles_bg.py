#!/usr/bin/env python3
import os
import sys

sys.path.insert(0, os.path.join("..", "pyconv"))
sys.path.insert(0, os.path.join("..", ".."))
from gfxutil import load_file, render_tile_grid
from romconv.games.zaxxon import ZAXXON_FILES, decode_bg_tiles

ROM_SET = os.path.join("..", "..", "..", "romszip", "zaxxon.zip")

OUTPUT_IMAGE_FILE = "zaxxon_bg_preview.png"
TILES_PER_ROW = 32
PREVIEW_PALETTE = {
    0: (0, 0, 0), 1: (255, 85, 85), 2: (85, 255, 85), 3: (255, 255, 85),
    4: (85, 85, 255), 5: (255, 85, 255), 6: (85, 255, 255), 7: (255, 255, 255),
}

def main():
    bg1 = load_file(ROM_SET, [ZAXXON_FILES["gfx_bg_1"][0]], ZAXXON_FILES["gfx_bg_1"][1])
    bg2 = load_file(ROM_SET, [ZAXXON_FILES["gfx_bg_2"][0]], ZAXXON_FILES["gfx_bg_2"][1])
    bg3 = load_file(ROM_SET, [ZAXXON_FILES["gfx_bg_3"][0]], ZAXXON_FILES["gfx_bg_3"][1])
    tiles = decode_bg_tiles(bg1 + bg2 + bg3)
    render_tile_grid(tiles, 8, 8, PREVIEW_PALETTE, TILES_PER_ROW, OUTPUT_IMAGE_FILE)

if __name__ == "__main__":
    main()
