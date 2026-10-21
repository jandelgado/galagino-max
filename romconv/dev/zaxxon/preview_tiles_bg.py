#!/usr/bin/env python3
"""Zaxxon background tiles as a PNG grid in build/preview/zaxxon/."""
from dev.preview import PALETTE, context, grid, preview_dir
from romconv.games.zaxxon import ZAXXON_FILES, decode_bg_tiles

OUTPUT_IMAGE_FILE = preview_dir("zaxxon") / "zaxxon_bg_preview.png"
TILES_PER_ROW = 32


def main():
    ctx = context("zaxxon")
    gfx = b"".join(ctx.rom(*ZAXXON_FILES[key]) for key in ("gfx_bg_1", "gfx_bg_2", "gfx_bg_3"))
    grid(decode_bg_tiles(gfx), 8, 8, PALETTE, TILES_PER_ROW, OUTPUT_IMAGE_FILE)


if __name__ == "__main__":
    main()
