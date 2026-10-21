# Mega Zone (Konami 1983), MAME konami/megazone.cpp, set megazone.
# Chars: 8x8 4bpp packed, 512 codes, stored per native x column
#   (uint32_t[8]): word[nx] nibble k = pixel at native y k, for the ROT90
#   line render (panel row = native x).
# Sprites: 16x16 4bpp, 256 codes, native [16][8], low nibble = even x.
# Palette: 32 RGB565 from the palette PROM (MAME resnet weights, RGB565
#   byte swapped), sprite and char lookups (MAME megazone_state::palette).
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.emit.write import write_asset
from romconv.gfx.color import rgb565_swapped
from romconv.gfx.gfxutil import column_words, mame_decode, pack_pairs
from romconv.gfx.resnet import combine, resistor_weights
from romconv.plan.budget import log_sizes

_CHAR_COUNT = 512
_SPRITE_COUNT = 256
_SPR_PLANES = (0x4000 * 8 + 4, 0x4000 * 8 + 0, 4, 0)
_SPR_XOFFS = (0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195)
_SPR_YOFFS = tuple([8 * i for i in range(8)] + [256 + 8 * i for i in range(8)])


def _convert(ctx: Context) -> None:
    def rom(name, sha1):
        return ctx.rom(name, sha1)

    # main KONAMI-1 0x6000-0xffff (0x4000-0x5fff debug ROM, not loaded)
    main = (rom("319_l07.11h", "ecdcdd085020f8ffe87a574832f7cedb9bcacef9")
            + rom("319_l06.9h", "4c8688b7bde5ee1adfe6a0db7178cca046eca7f4")
            + rom("319_l05.8h", "d6373f5be06cd61b6d3ffbe36c160167ba9852f3")
            + rom("319_l04.7h", "389e2f5494284089d39249e91293f3998c2b22c0")
            + rom("319_l03.6h", "8c8fbb76a36108f4a0b3a50e8a9b1781508490f1"))
    sound = rom("319e02.6d", "3808d1b58fe152f8f5b49bf0aa40c53e9c9dd4bd")
    i8039 = rom("319e01.3a", "64f54621487291fbfe827fb4cecca299fd0db781")
    sprites = (rom("319e11.3e", "210aae91a3838e5f7c78747d9b7419d266538ffc")
               + rom("319e09.2e", "4c038e80d575988407252897a1f1bc6b76af597c")
               + rom("319e10.3d", "be2dd46cd0121cedad6dab90a22643798a3176ab")
               + rom("319e08.2d", "fc8c0ecd3b7f03d63b6c3143143986883345fa38"))
    chars = (rom("319_g12.8c", "faadcb20ee8b26b9ab0692df6a81e5423514863e")
             + rom("319_g13.10c", "1f6fbf804dacfa44cd11b4cf41d0bedb7f2ff6b6"))
    pal_prom = rom("319b18.a16", "aba459826a75ec07bc6d97580e934f58aa22f4f4")
    spr_lut = rom("319b16.c6", "c1478c31533a11421cd4579ccfdbb430e193d17b")
    chr_lut = rom("319b17.a11", "1c58eb91982d5f10511d54a83070e859ac57d2f1")

    # --- chars: charlayout, nibble packed, plane 0 = MSB ---
    chars_dec = mame_decode(chars, 8, 8, [0, 1, 2, 3], [4 * i for i in range(8)],
                            [32 * i for i in range(8)], 256, _CHAR_COUNT)
    char_words = column_words(chars_dec, 4)

    # --- sprites: spritelayout ---
    spr_dec = mame_decode(sprites, 16, 16, _SPR_PLANES, _SPR_XOFFS, _SPR_YOFFS, 512, _SPRITE_COUNT)
    spr_bytes = pack_pairs(spr_dec)

    # --- palette: compute_resistor_weights(0, 255, -1, 3 x {1000,470,220}, 3 x .., 2 x {470,220}, pulldown 1000) ---
    (rw, gw, bw), _ = resistor_weights(0, 255, -1.0, [([1000, 470, 220], 1000, 0),
                                                       ([1000, 470, 220], 1000, 0),
                                                       ([470, 220], 1000, 0)])
    rgb888 = []
    for c in pal_prom:
        r = combine(rw, c & 1, (c >> 1) & 1, (c >> 2) & 1)
        g = combine(gw, (c >> 3) & 1, (c >> 4) & 1, (c >> 5) & 1)
        b = combine(bw, (c >> 6) & 1, (c >> 7) & 1)
        rgb888.append((r, g, b))
    rgb565 = [rgb565_swapped(*c) for c in rgb888]
    # lookup: sprites 0x000-0x0ff -> colors 0-15, chars 0x100-0x1ff -> colors 16-31
    lut = [b & 0x0F for b in spr_lut] + [(b & 0x0F) | 0x10 for b in chr_lut]

    def write(name, fn):
        write_asset(ctx, name, None, fn)

    write("megazone_rom_main.h",
          lambda f: emit_compressed(f, "megazone_rom_main", "unsigned char", "", len(main), list(main)))
    write("megazone_rom_sound.h",
          lambda f: emit_compressed(f, "megazone_rom_sound", "unsigned char", "", len(sound), list(sound)))
    write("megazone_rom_i8039.h",
          lambda f: emit_compressed(f, "megazone_rom_i8039", "unsigned char", "", len(i8039), list(i8039)))
    write("megazone_chars.h",
          lambda f: emit_compressed(f, "megazone_chars", "uint32_t", "[8]", _CHAR_COUNT, char_words))
    write("megazone_sprites.h",
          lambda f: emit_compressed(f, "megazone_sprites", "unsigned char", "[16][8]", _SPRITE_COUNT, spr_bytes))

    def palette(f):
        emit_compressed(f, "megazone_rgb", "unsigned short", "", 32, rgb565)
        emit_compressed(f, "megazone_lut", "unsigned char", "", 512, lut)

    write("megazone_palette.h", palette)

    sizes = {"main": len(main), "sound": len(sound), "i8039": len(i8039), "chars": _CHAR_COUNT * 32,
             "sprites": _SPRITE_COUNT * 128, "rgb": 64, "lut": 512}
    log_sizes(ctx, sizes, 201500)


JOB = Job(name="megazone", convert=_convert, machine="megazone", zips=("megazone.zip",))
