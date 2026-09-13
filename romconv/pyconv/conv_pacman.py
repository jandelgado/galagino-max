#!/usr/bin/env python3
from convutil import run, info

info("--------- Convert Pacman ---------")
info("Pacman Unpack roms")
run("unpack.py", "pacman.zip")

info("Pacman CPU code")
run("romconv.py", "-c", "pacman_rom", "./roms/pacman.6e", "./roms/pacman.6f", "./roms/pacman.6h", "./roms/pacman.6j", "../source/src/machines/pacman/pacman_rom.h")

info("Pacman Tiles")
run("tileconv.py", "-c", "pacman_tilemap", "./roms/pacman.5e", "../source/src/machines/pacman/pacman_tilemap.h")

info("Pacman Sprites")
run("spriteconv.py", "-c", "pacman_sprites", "pacman", "./roms/pacman.5f", "../source/src/machines/pacman/pacman_spritemap.h")

info("Pacman Colormaps")
run("cmapconv.py", "pacman_colormap", "./roms/82s123.7f", "0", "./roms/82s126.4a", "../source/src/machines/pacman/pacman_cmap.h")

info("Pacman Audio")
run("audioconv.py", "pacman_wavetable", "./roms/82s126.1m", "../source/src/machines/pacman/pacman_wavetable.h")

info("--- Success ---")
