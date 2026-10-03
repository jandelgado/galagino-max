#!/usr/bin/env python3
from convutil import run, info

info("--------- Convert Digdug ---------")
info("Digdug Unpack roms")
run("unpack.py", "digdug.zip")

info("Digdug CPU code")
run("romconv.py", "-c", "digdug_rom_cpu1", "./roms/dd1a.1", "./roms/dd1a.2", "./roms/dd1a.3", "./roms/dd1a.4", "../source/src/machines/digdug/digdug_rom1.h")
run("romconv.py", "-c", "digdug_rom_cpu2", "./roms/dd1a.5", "./roms/dd1a.6", "../source/src/machines/digdug/digdug_rom2.h")
run("romconv.py", "-c", "digdug_rom_cpu3", "./roms/dd1.7", "../source/src/machines/digdug/digdug_rom3.h")
run("romconv.py", "-c", "digdug_playfield", "./roms/dd1.10b", "../source/src/machines/digdug/digdug_playfield.h")

info("Digdug Tiles")
run("tileconv.py", "-c", "digdug_tilemap", "./roms/dd1.9", "../source/src/machines/digdug/digdug_tilemap.h")
run("tileconv.py", "-c", "digdug_pftiles", "./roms/dd1.11", "../source/src/machines/digdug/digdug_pftiles.h")

info("Digdug Sprites")
run("spriteconv.py", "-c", "digdug_sprites", "digdug", "./roms/dd1.15", "./roms/dd1.14", "./roms/dd1.13", "./roms/dd1.12", "../source/src/machines/digdug/digdug_spritemap.h")

info("Digdug Colormaps")
run("cmapconv.py", "digdug_colormap_tiles", "./roms/136007.113", "0", "./roms/136007.112", "../source/src/machines/digdug/digdug_cmap_tiles.h")
run("cmapconv.py", "digdug_colormap_sprites", "./roms/136007.113", "16", "./roms/136007.111", "../source/src/machines/digdug/digdug_cmap_sprites.h")
run("cmapconv.py", "digdug_colormaps", "./roms/136007.113", "../source/src/machines/digdug/digdug_cmap.h")

info("Digdug Audio")
run("audioconv.py", "digdug_wavetable", "./roms/136007.110", "../source/src/machines/digdug/digdug_wavetable.h")

info("--- Success ---")
