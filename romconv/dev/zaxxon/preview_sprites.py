#!/usr/bin/env python3
"""Zaxxon sprite tiles as a PNG grid in build/preview/zaxxon/."""
from dev.preview import PALETTE, context, grid, preview_dir
from romconv.games.zaxxon import ZAXXON_FILES, decode_sprite_tiles

OUTPUT_IMAGE_FILE = preview_dir("zaxxon") / "zaxxon_sprites_preview.png"
TILES_PER_ROW = 8


def main():
    ctx = context("zaxxon")
    gfx = b"".join(ctx.rom(*ZAXXON_FILES[key]) for key in ("gfx_spr_1", "gfx_spr_2", "gfx_spr_3"))
    grid(decode_sprite_tiles(gfx), 32, 32, PALETTE, TILES_PER_ROW, OUTPUT_IMAGE_FILE)


if __name__ == "__main__":
    main()
