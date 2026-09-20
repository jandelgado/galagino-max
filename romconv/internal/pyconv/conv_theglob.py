#!/usr/bin/env python3
from convutil import run, info

info("--------- Convert TheGlob ---------")
info("TheGlob Unpack roms")
run("internal/unpack.py", "theglobp.zip")

info("TheGlob CPU code")
run("internal/romconv.py", "-c", "theglob_rom", "./roms/glob.u2", "./roms/glob.u3", "../source/src/machines/theglob/theglob_rom.h")

info("TheGlob Tiles")
run("internal/tileconv.py", "-c", "theglob_tilemap", "./roms/glob.5e", "../source/src/machines/theglob/theglob_tilemap.h")

info("TheGlob Sprites")
run("internal/spriteconv.py", "-c", "theglob_sprites", "pacman", "./roms/glob.5f", "../source/src/machines/theglob/theglob_spritemap.h")

info("TheGlob Colormaps")
run("internal/cmapconv.py", "theglob_colormap", "./roms/glob.7f", "0", "./roms/glob.4a", "../source/src/machines/theglob/theglob_cmap.h")

info("TheGlob Audio")
run("internal/audioconv.py", "theglob_wavetable", "./roms/82s126.1m", "../source/src/machines/theglob/theglob_wavetable.h")

info("--- Success ---")
