#!/usr/bin/env python3
from convutil import run, info

info("--------- Convert Frogger ---------")
info("Frogger Unpack roms")
run("unpack.py", "frogger.zip")

info("Frogger CPU code")
run("romconv.py", "-c", "frogger_rom_cpu1", "./roms/frogger.26", "./roms/frogger.27", "./roms/frsm3.7", "../source/src/machines/frogger/frogger_rom1.h")
run("romconv.py", "-c", "frogger_rom_cpu2", "./roms/frogger.608", "./roms/frogger.609", "./roms/frogger.610", "../source/src/machines/frogger/frogger_rom2.h")

info("Frogger Tiles")
run("tileconv.py", "-c", "frogger_tilemap", "./roms/frogger.606", "./roms/frogger.607", "../source/src/machines/frogger/frogger_tilemap.h")

info("Frogger Sprites")
run("spriteconv.py", "-c", "frogger_sprites", "frogger", "./roms/frogger.606", "./roms/frogger.607", "../source/src/machines/frogger/frogger_spritemap.h")

info("Frogger Colormaps")
run("cmapconv.py", "frogger_colormap", "./roms/pr-91.6l", "../source/src/machines/frogger/frogger_cmap.h")

info("--- Success ---")
