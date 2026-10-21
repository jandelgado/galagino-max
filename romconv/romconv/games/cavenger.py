# Cosmic Avenger (Universal 1981), MAME universal/ladybug.cpp, set cavenger.
# Display ROT0 240x192, no rotation: every gfx row is one native line.
# Chars: 8x8 2bpp, 512 codes, uint16_t[8]: word[y] bits 2x = pixel x.
# Sprites 16x16 2bpp, 128 codes, uint32_t[16]; sprites 8x8, 512 codes,
#   uint16_t[8] (same gfx region, MAME spritelayout / spritelayout2).
# Palette: 32 RGB565 (inverted PROM, 470/220 resnet) + sprite lookup.
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.gfx.color import rgb565_swapped
from romconv.gfx.gfxutil import mame_decode
from romconv.gfx.resnet import combine, resistor_weights
from romconv.plan.budget import log_sizes

_CHAR_COUNT, _BIG_COUNT, _SMALL_COUNT = 512, 128, 512
_MAIN = (("1.c4", "f23bd6b9f427c26ac996a5c8ba29f356cf45c78a"),
         ("2.d4", "170e3f8be592dcccb8868474f40f8f2223e8a8b5"),
         ("3.e4", "4b0f1b38a5837b7ffc9aec6c28c6eb72cfa46226"),
         ("4.h4", "f8a7ea799d8ff9b4f830d064bb2f34a76729c336"),
         ("5.j4", "622c90a6c3f0adc24fe8a1d4969075cbd55add4e"),
         ("6.k4", "ec4981fe34abf992acbd6325b2c756c58ff80b04"))


def _rows2bpp(gfx):
    return [sum(p << (2 * x) for x, p in enumerate(row)) for t in gfx for row in t]


def _bit(v, n):
    return (~v >> n) & 1


def _rev4(v):
    return ((v & 1) << 3) | ((v & 2) << 1) | ((v & 4) >> 1) | ((v & 8) >> 3)


def _convert(ctx: Context) -> None:
    main = b"".join(ctx.rom(n, s) for n, s in _MAIN)
    chars = (ctx.rom("9.f7", "20eaa866b7700535312fd415edaea94408ff3e3d") +
             ctx.rom("0.h7", "bc8c52c6ba919287773ff6a4ec793ebd95176130"))
    sprites = ctx.rom("8.l7", "85f78d5a1e5782587bb66ad101a94fd0d62fb790") + bytes(0x1000)   # 1000-1fff empty
    prom_pal = ctx.rom("10-2.k1", "03175ee7f8e11896a89d7cc0d614a78a49923627")
    prom_spr = ctx.rom("10-1.f4", "4c9c76826f3a2a631d01fd2531d55318172b0c12")

    chars_dec = mame_decode(chars, 8, 8, [0, 512 * 8 * 8], [7, 6, 5, 4, 3, 2, 1, 0],
                            [8 * i for i in range(8)], 8 * 8, _CHAR_COUNT)
    big_dec = mame_decode(sprites, 16, 16, [1, 0],
                          [0, 2, 4, 6, 8, 10, 12, 14] + [128 + 2 * i for i in range(8)],
                          [16 * i for i in (23, 22, 21, 20, 19, 18, 17, 16, 7, 6, 5, 4, 3, 2, 1, 0)],
                          64 * 8, _BIG_COUNT)
    small_dec = mame_decode(sprites, 8, 8, [1, 0], [0, 2, 4, 6, 8, 10, 12, 14],
                            [16 * i for i in (7, 6, 5, 4, 3, 2, 1, 0)], 16 * 8, _SMALL_COUNT)

    (w,), _ = resistor_weights(0, 255, -1.0, [([470, 220], 470, 0)])
    rgb565 = [rgb565_swapped(combine(w, _bit(c, 0), _bit(c, 5)),
                             combine(w, _bit(c, 2), _bit(c, 6)),
                             combine(w, _bit(c, 4), _bit(c, 7))) for c in prom_pal]

    # MAME pens 0x20 + i: colors 0-7 from the low nibble, 8-15 from the high nibble
    spr_lut = [_rev4(b & 0x0F) for b in prom_spr] + [_rev4(b >> 4) for b in prom_spr]

    with ctx.header("cavenger_rom.h") as f:
        emit_compressed(f, "cavenger_rom", "unsigned char", "", len(main), main)
    ctx.log.info("wrote cavenger_rom.h")
    with ctx.header("cavenger_chars.h") as f:
        emit_compressed(f, "cavenger_chars", "unsigned short", "[8]", _CHAR_COUNT, _rows2bpp(chars_dec))
    ctx.log.info("wrote cavenger_chars.h")
    with ctx.header("cavenger_sprites.h") as f:
        emit_compressed(f, "cavenger_sprites", "uint32_t", "[16]", _BIG_COUNT, _rows2bpp(big_dec))
        emit_compressed(f, "cavenger_small", "unsigned short", "[8]", _SMALL_COUNT, _rows2bpp(small_dec))
    ctx.log.info("wrote cavenger_sprites.h")
    with ctx.header("cavenger_palette.h") as f:
        emit_compressed(f, "cavenger_rgb", "unsigned short", "", 32, rgb565)
        emit_compressed(f, "cavenger_lut", "unsigned char", "", 64, spr_lut)
    ctx.log.info("wrote cavenger_palette.h")

    sizes = {"main": len(main), "chars": _CHAR_COUNT * 16, "sprites": _BIG_COUNT * 64,
             "small": _SMALL_COUNT * 16, "rgb": 64, "lut": 64}
    log_sizes(ctx, sizes)


JOB = Job(name="cavenger", convert=_convert, machine="cavenger", zips=("cavenger.zip",))
