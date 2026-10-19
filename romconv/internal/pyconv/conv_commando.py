#!/usr/bin/env python3
from functools import partial
from convutil import run, info, fatal

run_in_workdir = partial(run, cwd="internal/commando")
OUT = "../source/src/machines/commando/"
TILES_A = 418  # commando.h TILES_A

info("--------- Convert Commando ---------")
info("Commando Unpack roms")
run("internal/unpack.py", "commando.zip")

# Arena holds the bg tiles (128 KB, compress 3x); the ROMs, chars and
# sprite bank 1 stay PLAIN (flash): they gain least per Arena byte.
info("Commando CPU code (encrypted opcodes, decrypted at run time)")
run("internal/romconv.py", "commando_rom", "./roms/cm04.9m", "./roms/cm03.8m", OUT + "commando_rom.h")

info("Commando sound CPU code")
run("internal/romconv.py", "commando_sound", "./roms/cm02.9f", OUT + "commando_sound.h")

info("Commando Tiles")
run("internal/tileconv.py", "-p", "commando_chars", "./roms/vt01.5d", OUT + "commando_chars.h")
# split at TILES_A (commando.h): part a fills block A, part b block B
run("internal/tileconv.py", "-s", str(TILES_A), "commando_tiles", "./roms/vt11.5a", "./roms/vt12.6a",
    "./roms/vt13.7a", "./roms/vt14.8a", "./roms/vt15.9a", "./roms/vt16.10a", OUT + "commando_tiles.h")

info("Commando Sprites (one asset per 256 sprite bank)")
for bank, (low, high) in enumerate([("vt05.7e", "vt08.7h"), ("vt06.8e", "vt09.8h"), ("vt07.9e", "vt10.9h")]):
    plain = ["-p"] if bank == 1 else []
    run("internal/spriteconv.py", *plain, "commando_sprites%d" % bank, "1942", "./roms/" + low,
        "./roms/" + high, OUT + "commando_sprites%d.h" % bank)

info("Commando Colormaps")
run_in_workdir("commando_cmap_convert.py")

info("Commando C64 SID music")
run_in_workdir("commando_sid_convert.py")

# Arena residents (bytes) per block, see commando.cpp ctor:
# A: sprites0, tiles 0..TILES_A-1, cmaps (128 + 64 + 64) x 2, SID tune RAM
#    window (commando.h TUNE_RAM_SIZE); B: sprites2, tiles TILES_A..1023
ARENA = {"sprites": 2 * 32768, "tiles": 1024 * 128, "cmaps": 512, "tune_ram": 4096}
BLOCK_A = 32768 + TILES_A * 128 + 512 + 4096
BLOCK_B = 32768 + (1024 - TILES_A) * 128
if BLOCK_A > 91000 or BLOCK_B > 110500:
    fatal(f"Commando blocks exceed Arena: A {BLOCK_A} of 91000, B {BLOCK_B} of 110500")
total = sum(ARENA.values())
for name, size in ARENA.items():
    info(f"  arena {name:9s} {size:6d}")
info(f"  arena total     {total:6d} of 201500")
if total >= 201500:
    fatal("Commando assets exceed Arena::CAPACITY")

info("--- Success ---")
