#!/usr/bin/env python3
from convutil import run, info

info("--------- Convert Bagman ---------")
info("Bagman Unpack roms")
run("unpack.py", "bagmanm2.zip")

info("Bagman CPU code")
run("romconv.py", "-c", "bagman_rom_cpu", "./roms/bagmanm2.1", "./roms/bagmanm2.2", "./roms/bagmanm2.3", "../source/src/machines/bagman/bagman_rom.h")

info("Bagman Tiles")
run("tileconv.py", "-c", "bagman", "bagman_tilemap", "./roms/bagmanm2.9", "./roms/bagmanm2.7", "../source/src/machines/bagman/bagman_tilemap.h")

info("Bagman Sprites")
run("spriteconv.py", "-c", "bagman_sprites", "bagman", "./roms/bagmanm2.9", "./roms/bagmanm2.7", "../source/src/machines/bagman/bagman_spritemap.h")

info("Bagman Colormaps")
run("cmapconv.py", "bagman_colormap", "./roms/bagmanmc.clr", "../source/src/machines/bagman/bagman_cmap.h")

info("--- Success ---")
