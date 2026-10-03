#!/usr/bin/env python3
from convutil import run, info

info("--------- Convert Crush ---------")
info("Crush Unpack roms")
run("unpack.py", "crush.zip")

info("Crush CPU code")
run("romconv.py", "-c", "crush_rom", "./roms/crushkrl.6e", "./roms/crushkrl.6f", "./roms/crushkrl.6h", "./roms/crushkrl.6j", "../source/src/machines/crush/crush_rom.h")

info("Crush Tiles")
run("tileconv.py", "-c", "crush_tilemap", "./roms/maketrax.5e", "../source/src/machines/crush/crush_tilemap.h")

info("Crush Sprites")
run("spriteconv.py", "-c", "crush_sprites", "crush", "./roms/maketrax.5f", "../source/src/machines/crush/crush_spritemap.h")

info("Crush Colormaps")
run("cmapconv.py", "crush_colormap", "./roms/82s123.7f", "0", "./roms/2s140.4a", "../source/src/machines/crush/crush_cmap.h")

info("Crush Audio")
run("audioconv.py", "crush_wavetable", "./roms/82s126.1m", "../source/src/machines/crush/crush_wavetable.h")

info("--- Success ---")
