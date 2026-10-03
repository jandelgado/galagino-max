#!/usr/bin/env python3
from convutil import run, info

info("--------- Convert Dkong ---------")
info("Dkong Unpack roms")
run("unpack.py", "dkong.zip")

info("Dkong CPU code")
run("romconv.py", "-c", "dkong_rom_cpu1", "./roms/c_5et_g.bin", "./roms/c_5ct_g.bin", "./roms/c_5bt_g.bin", "./roms/c_5at_g.bin", "../source/src/machines/dkong/dkong_rom1.h")
run("romconv.py", "-c", "dkong_rom_cpu2", "./roms/s_3i_b.bin", "./roms/s_3j_b.bin", "../source/src/machines/dkong/dkong_rom2.h")

info("Dkong Tiles")
run("tileconv.py", "-c", "dkong_tilemap", "./roms/v_5h_b.bin", "./roms/v_3pt.bin", "../source/src/machines/dkong/dkong_tilemap.h")

info("Dkong Sprites")
run("spriteconv.py", "-c", "dkong_sprites", "dkong", "./roms/l_4m_b.bin", "./roms/l_4n_b.bin", "./roms/l_4r_b.bin", "./roms/l_4s_b.bin", "../source/src/machines/dkong/dkong_spritemap.h")

info("Dkong Colormaps")
run("cmapconv.py", "-c", "dkong_colormap", "./roms/c-2k.bpr", "./roms/c-2j.bpr", "0", "./roms/v-5e.bpr", "../source/src/machines/dkong/dkong_cmap.h")

info("--- Success ---")
