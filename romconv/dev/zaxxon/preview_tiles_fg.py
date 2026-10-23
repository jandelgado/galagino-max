#!/usr/bin/env python3
"""Zaxxon foreground tiles as a PNG grid in build/preview/zaxxon/."""
from dev.preview import PALETTE, context, grid, preview_dir
from romconv.games.zaxxon import ZAXXON_FILES, decode_char_tiles

OUTPUT_IMAGE_FILE = preview_dir("zaxxon") / "zaxxon_fg_preview.png"
TILES_PER_ROW = 16


def main():
    ctx = context("zaxxon")
    gfx = b"".join(ctx.rom(*ZAXXON_FILES[key]) for key in ("gfx_tx_1", "gfx_tx_2"))
    grid(decode_char_tiles(gfx), 8, 8, PALETTE, TILES_PER_ROW, OUTPUT_IMAGE_FILE)


if __name__ == "__main__":
    main()
