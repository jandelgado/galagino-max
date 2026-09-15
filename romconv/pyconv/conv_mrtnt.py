#!/usr/bin/env python3
from convutil import run, info

info("--------- Convert MrTNT ---------")
info("MrTNT Unpack roms")
run("unpack.py", "mrtnt.zip")

info("MrTNT CPU code")
run("romconv.py", "-d", "-c", "mrtnt_rom", "./roms/tnt.1", "./roms/tnt.2", "./roms/tnt.3", "./roms/tnt.4", "../source/src/machines/mrtnt/mrtnt_rom.h")

info("MrTNT Tiles")
run("tileconv.py", "-c", "mrtnt_tilemap", "./roms/tnt.5", "../source/src/machines/mrtnt/mrtnt_tilemap.h")

info("MrTNT Sprites")
run("spriteconv.py", "-c", "mrtnt_sprites", "mrtnt", "./roms/tnt.6", "../source/src/machines/mrtnt/mrtnt_spritemap.h")

info("MrTNT Colormaps")
run("cmapconv.py", "mrtnt_colormap", "./roms/82s123.7f", "0", "./roms/82s126.4a", "../source/src/machines/mrtnt/mrtnt_cmap.h")

info("MrTNT Audio")
run("audioconv.py", "mrtnt_wavetable", "./roms/82s126.1m", "../source/src/machines/mrtnt/mrtnt_wavetable.h")

info("--- Success ---")
