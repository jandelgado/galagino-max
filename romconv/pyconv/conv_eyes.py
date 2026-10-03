#!/usr/bin/env python3
from convutil import run, info

info("--------- Convert Eyes ---------")
info("Eyes Unpack roms")
run("unpack.py", "eyes.zip")

info("Eyes CPU code")
run("romconv.py", "-d", "-c", "eyes_rom", "./roms/d7", "./roms/e7", "./roms/f7", "./roms/h7", "../source/src/machines/eyes/eyes_rom.h")

info("Eyes Tiles")
run("tileconv.py", "-c", "eyes_tilemap", "./roms/d5", "../source/src/machines/eyes/eyes_tilemap.h")

info("Eyes Sprites")
run("spriteconv.py", "-c", "eyes_sprites", "eyes", "./roms/e5", "../source/src/machines/eyes/eyes_spritemap.h")

info("Eyes Colormaps")
run("cmapconv.py", "eyes_colormap", "./roms/82s123.7f", "0", "./roms/82s129.4a", "../source/src/machines/eyes/eyes_cmap.h")

info("Eyes Audio")
run("audioconv.py", "eyes_wavetable", "./roms/82s126.1m", "../source/src/machines/eyes/eyes_wavetable.h")

info("--- Success ---")
