#!/usr/bin/env python3
from convutil import run, info

info("--------- Convert Anteater ---------")
info("Anteater Unpack roms")
run("internal/unpack.py", "anteater.zip")

info("Anteater CPU code")
run("internal/romconv.py", "-c", "anteater_rom_cpu1", "./roms/ra1-2c", "./roms/ra1-2e", "./roms/ra1-2f", "./roms/ra1-2h", "../source/src/machines/anteater/anteater_rom1.h")
run("internal/romconv.py", "-c", "anteater_rom_cpu2", "./roms/ra4-5c", "./roms/ra4-5d", "../source/src/machines/anteater/anteater_rom2.h")

info("Anteater Tiles")
run("internal/tileconv.py", "-c", "anteater", "anteater_tilemap", "./roms/ra6-5f", "./roms/ra6-5h", "../source/src/machines/anteater/anteater_tilemap.h")

info("Anteater Sprites")
run("internal/spriteconv.py", "-c", "anteater_sprites", "anteater", "./roms/ra6-5f", "./roms/ra6-5h", "../source/src/machines/anteater/anteater_spritemap.h")

info("Anteater Colormaps")
run("internal/cmapconv.py", "anteater_colormap", "./roms/colr6f.cpu", "../source/src/machines/anteater/anteater_cmap.h")

info("--- Success ---")
