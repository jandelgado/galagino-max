#!/usr/bin/env python3
from convutil import run, info

info("--------- Convert Van Van Car ---------")
info("Van Van Car Unpack roms")
run("unpack.py", "vanvan.zip")

info("Van Van Car CPU code (main bank 0x0000-0x3fff)")
run("romconv.py", "-c", "vanvan_rom", "./roms/van-1.50", "./roms/van-2.51", "./roms/van-3.52", "./roms/van-4.53", "../source/src/machines/vanvan/vanvan_rom.h")

info("Van Van Car CPU code (extra bank 0x8000-0x8fff)")
run("romconv.py", "-c", "vanvan_rom2", "./roms/van-5.39", "../source/src/machines/vanvan/vanvan_rom2.h")

info("Van Van Car Tiles")
run("tileconv.py", "-c", "vanvan_tilemap", "./roms/van-20.18", "../source/src/machines/vanvan/vanvan_tilemap.h")

info("Van Van Car Sprites")
run("spriteconv.py", "-c", "vanvan_sprites", "pacman", "./roms/van-21.19", "../source/src/machines/vanvan/vanvan_spritemap.h")

info("Van Van Car Colormaps")
run("cmapconv.py", "vanvan_colormap", "./roms/6331-1.6", "0", "./roms/6301-1.37", "../source/src/machines/vanvan/vanvan_cmap.h")

info("--- Success ---")
