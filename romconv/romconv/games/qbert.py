# Q*bert (Gottlieb 1982), MAME gottlieb/gottlieb.cpp, set qbert.
# The 8088 program is not emulated: romconv/dev/qbert/translate.py turned it
# into source/src/machines/qbert/qbert_cpu.cpp once. The code still reads its
# data tables from the ROM image emitted here.
# Tiles: 8x8 4bpp (gfx_8x8x4_packed_msb), 256 codes, one uint32_t per native
#   x column, nibble k = pixel at native y k (ROT270: native x = panel line).
# Sprites: 16x16 4bpp (spr_layout, qb-fg3 = plane 0 = MSB), 256 codes, two
#   words per native x column (y 0-7, y 8-15), same nibble order.
from collections.abc import Callable

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.emit.write import write_asset
from romconv.gfx.gfxutil import column_words, mame_decode, pack_pens
from romconv.plan.budget import log_sizes

_TILE_COUNT = 256
_SPRITE_COUNT = 256
_SPR_PLANES = tuple(n * 0x2000 * 8 for n in range(4))

_MAIN = (("qb-rom2.bin", "4cfc5d52dd6c82163e035af82d6112c0c93a3797"),   # 0xa000
         ("qb-rom1.bin", "ca6acdef1c9e06b33efe1f0a2df2dfb03723cfbe"),   # 0xc000
         ("qb-rom0.bin", "7f8f66d1e6a7905e93cce07fc92e8801370b7194"))   # 0xe000
_SOUND = (("qb-snd1.bin", "8b7d03fbf2ebaa71b3a7e2f636a0d1bb9b796e43"),  # 0x7000
          ("qb-snd2.bin", "09d8053e7e99679b602dcda230d64db7fe6cb7f5"))  # 0x7800
_BG = (("qb-bg0.bin", "12aa6df499eb6996ee35f56acac403ff6290f844"),
       ("qb-bg1.bin", "5bb67e333255c0ea679ab4312256a8a71a950db8"))
_FG = (("qb-fg3.bin", "ae16087a6ceec84551b5d7aae4036e0ed432cbb7"),
       ("qb-fg2.bin", "06894a1474c79c1274efbd32d7371179e7e0a661"),
       ("qb-fg1.bin", "f7f26b879aa8b964ff6311136ed8157e44de736c"),
       ("qb-fg0.bin", "807d16459838f129e10b913890bbc95065d5dd40"))


def _join(read: Callable[[str], bytes], roms) -> bytes:
    return b"".join(read(name) for name, _ in roms)


def main_image(read: Callable[[str], bytes]) -> bytes:
    """8088 address space 0xa000-0xffff."""
    return _join(read, _MAIN)


def tile_words(bg: bytes) -> list[int]:
    tiles = mame_decode(bg, 8, 8, [0, 1, 2, 3], [4 * i for i in range(8)], [32 * i for i in range(8)],
                        256, _TILE_COUNT)
    return column_words(tiles, 4)


def decode_sprites(fg: bytes) -> list[list[list[int]]]:
    return mame_decode(fg, 16, 16, _SPR_PLANES, list(range(16)), [16 * i for i in range(16)], 256,
                       _SPRITE_COUNT)


def sprite_words(sprites) -> list[int]:
    return [pack_pens([s[y][x] for y in range(h * 8, h * 8 + 8)], 4)
            for s in sprites for x in range(16) for h in range(2)]


def _convert(ctx: Context) -> None:
    def read(name: str) -> bytes:
        sha1 = dict(_MAIN + _SOUND + _BG + _FG)[name]
        return ctx.rom(name, sha1)

    main = main_image(read)
    sound = _join(read, _SOUND)
    tiles = tile_words(_join(read, _BG))
    sprites = sprite_words(decode_sprites(_join(read, _FG)))

    def write(name, fn):
        write_asset(ctx, name, None, fn)

    def rom(f):
        emit_compressed(f, "qbert_main_rom", "unsigned char", "", len(main), main)
        emit_compressed(f, "qbert_sound_rom", "unsigned char", "", len(sound), sound)

    def gfx(f):
        emit_compressed(f, "qbert_tiles", "uint32_t", "[8]", _TILE_COUNT, tiles)
        emit_compressed(f, "qbert_sprites", "uint32_t", "[32]", _SPRITE_COUNT, sprites)

    write("qbert_rom.h", rom)
    write("qbert_gfx.h", gfx)
    log_sizes(ctx, {"main": len(main), "sound": len(sound), "tiles": _TILE_COUNT * 32,
                    "sprites": _SPRITE_COUNT * 128})


JOB = Job(name="qbert", convert=_convert, machine="qbert", zips=("qbert.zip",))
