#!/usr/bin/env python3
from convutil import run, info

info("--------- Convert Galaga ---------")
info("Galaga Unpack roms")
run("internal/unpack.py", "galaga.zip")

info("Galaga CPU code")
run("internal/romconv.py", "-p", "-c", "galaga_rom_cpu1", "./roms/gg1_1b.3p", "./roms/gg1_2b.3m", "./roms/gg1_3.2m", "./roms/gg1_4b.2l", "../source/src/machines/galaga/galaga_rom1.h")
run("internal/romconv.py", "-c", "galaga_rom_cpu2", "./roms/gg1_5b.3f", "../source/src/machines/galaga/galaga_rom2.h")
run("internal/romconv.py", "-c", "galaga_rom_cpu3", "./roms/gg1_7b.2c", "../source/src/machines/galaga/galaga_rom3.h")

info("Galaga Tiles")
run("internal/tileconv.py", "-c", "galaga_tilemap", "./roms/gg1_9.4l", "../source/src/machines/galaga/galaga_tilemap.h")

info("Galaga Sprites")
run("internal/spriteconv.py", "-c", "galaga_sprites", "galaga", "./roms/gg1_11.4d", "./roms/gg1_10.4f", "../source/src/machines/galaga/galaga_spritemap.h")

info("Galaga Colormaps")
run("internal/cmapconv.py", "galaga_colormap_sprites", "./roms/prom-5.5n", "0", "./roms/prom-3.1c", "../source/src/machines/galaga/galaga_cmap_sprites.h")
run("internal/cmapconv.py", "galaga_colormap_tiles", "./roms/prom-5.5n", "16", "./roms/prom-4.2n", "../source/src/machines/galaga/galaga_cmap_tiles.h")

info("Galaga Audio")
run("internal/audioconv.py", "galaga_wavetable", "./roms/prom-1.1d", "../source/src/machines/galaga/galaga_wavetable.h")

info("--- Success ---")
