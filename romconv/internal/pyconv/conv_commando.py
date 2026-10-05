#!/usr/bin/env python3
from functools import partial
from convutil import run, info, fatal

run_in_workdir = partial(run, cwd="internal/commando")
OUT = "../source/src/machines/commando/"

info("--------- Convert Commando ---------")
info("Commando Unpack roms")
run("internal/unpack.py", "commando.zip")

info("Commando CPU code (encrypted opcodes, decrypted at run time)")
run("internal/romconv.py", "-c", "commando_rom", "./roms/cm04.9m", "./roms/cm03.8m", OUT + "commando_rom.h")

info("Commando Tiles")
run("internal/tileconv.py", "commando_chars", "./roms/vt01.5d", OUT + "commando_chars.h")
# 128 KB, stays in flash (plain), read sparsely per visible tile
run("internal/tileconv.py", "commando_tiles", "./roms/vt11.5a", "./roms/vt12.6a", "./roms/vt13.7a",
    "./roms/vt14.8a", "./roms/vt15.9a", "./roms/vt16.10a", OUT + "commando_tiles.h")

info("Commando Sprites (one asset per 256 sprite bank)")
for bank, (low, high) in enumerate([("vt05.7e", "vt08.7h"), ("vt06.8e", "vt09.8h"), ("vt07.9e", "vt10.9h")]):
    run("internal/spriteconv.py", "commando_sprites%d" % bank, "1942", "./roms/" + low, "./roms/" + high,
        OUT + "commando_sprites%d.h" % bank)

info("Commando Colormaps")
run_in_workdir("commando_cmap_convert.py")

info("Commando C64 SID music")
run_in_workdir("commando_sid_convert.py")

# Arena residents (bytes): rom, sprites 3 x 256 x 128, chars 1024 x 16,
# cmaps (128 + 64 + 64) x 2, SID tune RAM window (commando.h TUNE_RAM_SIZE)
ARENA = {"rom": 49152, "sprites": 3 * 32768, "chars": 16384, "cmaps": 512, "tune_ram": 4096}
total = sum(ARENA.values())
for name, size in ARENA.items():
    info(f"  arena {name:9s} {size:6d}")
info(f"  arena total     {total:6d} of 182000")
if total >= 182000:
    fatal("Commando assets exceed Arena::CAPACITY")

info("--- Success ---")
