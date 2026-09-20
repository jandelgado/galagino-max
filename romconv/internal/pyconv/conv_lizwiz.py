#!/usr/bin/env python3
from convutil import run, info

info("--------- Convert Lizwiz ---------")
info("Lizwiz Unpack roms")
run("internal/unpack.py", "lizwiz.zip")

info("Lizwiz CPU code")
run("internal/romconv.py", "-c", "lizwiz_rom", "./roms/6e.cpu", "./roms/6f.cpu", "./roms/6h.cpu", "./roms/6j.cpu", "./roms/wiza", "./roms/wizb", "../source/src/machines/lizwiz/lizwiz_rom.h")

info("Lizwiz Tiles")
run("internal/tileconv.py", "-c", "lizwiz_tilemap", "./roms/5e.cpu", "../source/src/machines/lizwiz/lizwiz_tilemap.h")

info("Lizwiz Sprites")
run("internal/spriteconv.py", "-c", "lizwiz_sprites", "lizwiz", "./roms/5f.cpu", "../source/src/machines/lizwiz/lizwiz_spritemap.h")

info("Lizwiz Colormaps")
run("internal/cmapconv.py", "lizwiz_colormap", "./roms/7f.cpu", "0", "./roms/4a.cpu", "../source/src/machines/lizwiz/lizwiz_cmap.h")

info("Lizwiz Audio")
run("internal/audioconv.py", "lizwiz_wavetable", "./roms/82s126.1m", "../source/src/machines/lizwiz/lizwiz_wavetable.h")

info("--- Success ---")
