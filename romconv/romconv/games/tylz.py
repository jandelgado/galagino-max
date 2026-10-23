# Tylz (Mylstar 1984 prototype), MAME gottlieb/gottlieb.cpp, set tylz.
# Q*bert's board unrotated (ROT0): the panel shows native scanlines, so tile
# and sprite words hold a native row (pen of x 0 in the lowest nibble).
# Tiles: 8x8 4bpp (gfx_8x8x4_packed_msb), 256 codes, one uint32_t per row.
# Sprites: 16x16 4bpp, layout as Q*bert (tylz.k12 = plane 0 = MSB), 256 codes,
#   two words per row (x 0-7, x 8-15).
# Palette: RAM through Q*bert's resistor network (qbert.levels).
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.emit.write import write_asset
from romconv.games import qbert
from romconv.gfx.gfxutil import mame_decode, pack_pens
from romconv.plan.budget import log_sizes

_TILE_COUNT = 256
_SPRITE_COUNT = 256

_MAIN = (("tylz.s4t", "52d72503b456b1411cd91724c6d524b78ca6fe03"),   # 0x8000
         ("tylz.s4b", "632896bfe7e14f93665671dbcc17b7cabc754a98"),   # 0xa000
         ("tylz.r4", "9908a2dd5109e632dff38b8b4b56160615355200"),    # 0xc000
         ("tylz.n4", "15db4d1a2fb70d8111940246cd7a8ae06403cac5"))    # 0xe000
_SOUND = (("tylz.f2", "94aee8e32bdc80bbc5dc1423ca97597bdb9d808c"),)  # 0x7000
# Votrax SC-01-A speech ROM (MAME votrsc01a)
_SC01 = ("sc01a.bin", "1d6da90b1807a01b5e186ef08476119a862b5e6d")
_BG = (("tylz.m6", "3b1e0371ba6d76ace893b92728c7a64b0e207d22"),
       ("tylz.n6", "772b2fb8efaaaccd465ab9d61fc295ef09e3a10d"))
_FG = (("tylz.k12", "71de79a92f23a1fcf4331c7fb1762870d74f7ea9"),   # region 0x0000
       ("tylz.j12", "cdaacd4d5a7644468c913076850f08d979cbb004"),   # 0x2000
       ("tylz.g12", "bc50069cefaafa302d559a2c2d3ab93f58900a24"),   # 0x4000
       ("tylz.f12", "851592d82680f9983f5d8c10c0a13fbaf31cfe41"))   # 0x6000


def _tile_words(bg: bytes) -> list[int]:
    tiles = mame_decode(bg, 8, 8, [0, 1, 2, 3], [4 * i for i in range(8)], [32 * i for i in range(8)],
                        256, _TILE_COUNT)
    return [pack_pens(row, 4) for t in tiles for row in t]


def _sprite_words(sprites) -> list[int]:
    return [pack_pens(row[h * 8:h * 8 + 8], 4) for s in sprites for row in s for h in range(2)]


def _convert(ctx: Context) -> None:
    def read(roms) -> bytes:
        return b"".join(ctx.rom(name, sha1) for name, sha1 in roms)

    main = read(_MAIN)
    sound = read(_SOUND)
    sc01 = bytes(ctx.rom(_SC01[0], _SC01[1], zip="votrax-qbert.zip"))
    tiles = _tile_words(read(_BG))
    sprites = _sprite_words(qbert.decode_sprites(read(_FG)))

    def rom(f):
        emit_compressed(f, "tylz_main_rom", "unsigned char", "", len(main), main)
        emit_compressed(f, "tylz_sound_rom", "unsigned char", "", len(sound), sound)
        emit_compressed(f, "tylz_sc01", "unsigned char", "", len(sc01), sc01)

    def gfx(f):
        emit_compressed(f, "tylz_tiles", "uint32_t", "[8]", _TILE_COUNT, tiles)
        emit_compressed(f, "tylz_sprites", "uint32_t", "[32]", _SPRITE_COUNT, sprites)
        emit_compressed(f, "tylz_levels", "unsigned char", "", 16, qbert.levels())

    write_asset(ctx, "tylz_rom.h", None, rom)
    write_asset(ctx, "tylz_gfx.h", None, gfx)
    log_sizes(ctx, {"main": len(main), "sound": len(sound), "sc01": len(sc01), "tiles": _TILE_COUNT * 32,
                    "sprites": _SPRITE_COUNT * 128, "levels": 16})


JOB = Job(name="tylz", convert=_convert, machine="tylz", zips=("tylz.zip", "votrax-qbert.zip"))
