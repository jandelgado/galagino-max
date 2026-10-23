# Krull (Gottlieb 1983), MAME gottlieb/gottlieb.cpp, set krull.
# Q*bert board: 8088 main CPU, 6502 rev. 1 sound (8 KB ROM, no speech).
# Tiles live in char RAM (init_ramtiles), so only sprites are converted;
# format and palette levels are Q*bert's.
from collections.abc import Callable

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.emit.write import write_asset
from romconv.games.qbert import decode_sprites, levels, sprite_words
from romconv.plan.budget import log_sizes

_SPRITE_COUNT = 256

# 8088 address space 0x1000-0xffff; 0x3000-0x5fff is I/O (stays zero)
_MAIN = ((0x1000, "gv-105_ram_2.c7", "9d70de35e4f0490dc4e601070993ad146f250dea"),
         (0x2000, "gv-105_ram_4.c9-10", "57ad5c904b9ac4bf7c7d828bf755bbcbba6a4fd7"),
         (0x6000, "gv-105_rom_4.c16", "b18270f4ad97743f6ff8c4cbc93e523c77a8e794"),
         (0x8000, "gv-105_rom_3.c14-15", "276c4008a013806b3989c529f41cbc358ec49fd6"),
         (0xa000, "gv-105_rom_2.c13-14", "1bae895fbdd658cfb56c53cc2139282cc1e778de"),
         (0xc000, "gv-105_rom_1.c12-13", "f5b74b196fe1bd9ab48336e0051cbf29c650cfc1"),
         (0xe000, "gv-105_rom_0.c11-12", "d691cbb46e8c3b71f9b1688d7fcef36df82aa854"))
_SOUND = (("snd1.bin", "f01cb64932493bf69d4fc75a7fa809ff6f6e4263"),   # 0x6000
          ("snd2.bin", "b886532828efc8cf442e2ee4ebbfe37acd489f62"))  # 0x7000
_FG = (("gv-105_fg_3.k7-8", "753476609c4bf4f0f0cd28d61fd8aef6967bda57"),  # plane 0 = MSB
       ("gv-105_fg_2.k6", "33d2c23a388b09c4a09b9893648c30fbd5482cc3"),
       ("gv-105_fg_1.k5", "0de86e632e5a9e6c1ec82550b15dc25a17ab7066"),
       ("gv-105_fg_0.k4", "d6d1b8aca8e9ee3bdc57f4474d22b405963909ec"))

_BASE = 0x1000
_IMAGE_SIZE = 0x10000 - _BASE


def _main_image(read: Callable[[str, str], bytes]) -> bytes:
    """8088 address space 0x1000-0xffff, gaps zero."""
    image = bytearray(_IMAGE_SIZE)
    for addr, name, sha1 in _MAIN:
        data = read(name, sha1)
        image[addr - _BASE:addr - _BASE + len(data)] = data
    return bytes(image)


def _convert(ctx: Context) -> None:
    main = _main_image(ctx.rom)
    sound = b"".join(ctx.rom(name, sha1) for name, sha1 in _SOUND)
    sprites = sprite_words(decode_sprites(b"".join(ctx.rom(name, sha1) for name, sha1 in _FG)))
    lv = levels()

    def rom(f):
        emit_compressed(f, "krull_main_rom", "unsigned char", "", len(main), main)
        emit_compressed(f, "krull_sound_rom", "unsigned char", "", len(sound), sound)

    def gfx(f):
        emit_compressed(f, "krull_sprites", "uint32_t", "[32]", _SPRITE_COUNT, sprites)
        emit_compressed(f, "krull_levels", "unsigned char", "", 16, lv)

    write_asset(ctx, "krull_rom.h", None, rom)
    write_asset(ctx, "krull_gfx.h", None, gfx)
    log_sizes(ctx, {"main": len(main), "sound": len(sound), "sprites": _SPRITE_COUNT * 128, "levels": 16})


JOB = Job(name="krull", convert=_convert, machine="krull", zips=("krull.zip",))
