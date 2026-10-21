# Mikie (Konami 1984), MAME konami/mikie.cpp, set mikie.
# Tiles: 8x8 4bpp, 512 codes, stored per native x line (uint32_t[8]):
#   word[nx] nibble k = pixel at native y k, for the ROT270 line render.
# Sprites: 16x16 4bpp native [16][8], low nibble = even x, 2 x 256 codes
#   (MAME gfx 1 at bit 0, gfx 2 at bit 8 of the same region).
# Palette: 256 RGB565 from the 3 PROMs (MAME resnet weights) + char and
#   sprite lookups; per-bank color maps are built at runtime.
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.gfx.color import rgb565_swapped
from romconv.gfx.gfxutil import mame_decode
from romconv.gfx.resnet import combine, resistor_weights

_TILE_COUNT = 512
_SPRITE_COUNT = 512

_SPR_PLANES = (0, 4, 256 * 128 * 8 + 0, 256 * 128 * 8 + 4)
_SPR_XOFFS = tuple([32 * 8 + i for i in range(4)] + [16 * 8 + i for i in range(4)]
                   + [i for i in range(4)] + [48 * 8 + i for i in range(4)])
_SPR_YOFFS = tuple([16 * i for i in range(8)] + [16 * (32 + i) for i in range(8)])


def _convert(ctx: Context) -> None:
    def rom(name, sha1):
        return ctx.rom(name, sha1)

    # main CPU 0x6000-0xffff (0x4000-0x5fff is an empty socket the game probes)
    main = (rom("n14.11c", "99220eeee4e7b88caa26f2d08502689e1f1fcdf8")
            + rom("o13.12a", "bd62783cb1ba4e7f0196f337280461bb7627f70f")
            + rom("o17.12d", "373a92b8412676ad9870cb562c73e47db7b40bea"))
    audio = rom("n10.6e", "b324b92aff70d7878160128611dd5fdec6949659")
    tiles = rom("o11.8i", "c84256ac5fd5e40b197651c56e303c69aae72950")
    sprites = (rom("001.f1", "873d49c1c2efbb222d1bf63396729d4b7d9477c3")
               + rom("003.f3", "0271567c5f5a6bb2eaffb9d5dc2af6b8142dc8a9")
               + rom("005.h1", "410bfd9146242254a920092e280af87586709527")
               + rom("007.h3", "31ca33f585fddb86131ef61de73e3563fa027455"))
    prom_r = rom("d19.1i", "4fce779947f9f318023c7c54a71a4751f6bb8eb1")
    prom_g = rom("d21.3i", "6f4fc3ef6b1b44278e7c8c1034ee4fbef90cf85a")
    prom_b = rom("d20.2i", "14236a831204d52cdf8c2ef318a565d6c5587ce0")
    char_lut = rom("d22.12h", "1525303589d7ed909bc6e2827fbaa2c16ad4030b")
    spr_lut = rom("d18.f9", "fedcc421a61d6623dc9c41b0a3e164efeb50ec7c")

    # --- tiles: gfx_8x8x4_packed_msb ---
    tiles_dec = mame_decode(tiles, 8, 8, [0, 1, 2, 3], [4 * i for i in range(8)],
                            [32 * i for i in range(8)], 256, _TILE_COUNT)
    tile_words = []
    for t in tiles_dec:
        for nx in range(8):
            w = 0
            for k in range(8):
                w |= t[k][nx] << (4 * k)
            tile_words.append(w)

    # --- sprites: MAME spritelayout, two gfx sets one byte apart ---
    spr_dec = []
    for bank_bit in (0, 8):
        spr_dec += mame_decode(sprites, 16, 16, _SPR_PLANES, _SPR_XOFFS, _SPR_YOFFS, 128 * 8, 256,
                               base_bit=bank_bit)
    spr_bytes = []
    for s in spr_dec:
        for row in s:
            for x in range(0, 16, 2):
                spr_bytes.append(row[x] | (row[x + 1] << 4))

    # --- palette: compute_resistor_weights(0, 255, -1, 4, {2200,1000,470,220}, w, 470, 0) ---
    (w,), _ = resistor_weights(0, 255, -1.0, [([2200, 1000, 470, 220], 470, 0)])

    def comp(v):
        return combine(w, v & 1, (v >> 1) & 1, (v >> 2) & 1, (v >> 3) & 1)

    rgb888 = [(comp(prom_r[i]), comp(prom_g[i]), comp(prom_b[i])) for i in range(256)]
    rgb565 = [rgb565_swapped(*c) for c in rgb888]
    lut = [b & 0x0F for b in char_lut] + [b & 0x0F for b in spr_lut]

    def write(name, fn):
        with ctx.header(name) as f:
            fn(f)
        ctx.log.info(f"wrote {name}")

    # uint16_t == unsigned short, uint32_t == unsigned int for emit_compressed
    write("mikie_rom_main.h", lambda f: emit_compressed(f, "mikie_rom_main", "unsigned char", "", len(main), main))
    write("mikie_rom_audio.h",
          lambda f: emit_compressed(f, "mikie_rom_audio", "unsigned char", "", len(audio), audio))
    write("mikie_tiles.h", lambda f: emit_compressed(f, "mikie_tiles", "uint32_t", "[8]", _TILE_COUNT, tile_words))
    write("mikie_sprites.h",
          lambda f: emit_compressed(f, "mikie_sprites", "unsigned char", "[16][8]", _SPRITE_COUNT, spr_bytes))

    def palette(f):
        emit_compressed(f, "mikie_rgb", "unsigned short", "", 256, rgb565)
        emit_compressed(f, "mikie_lut", "unsigned char", "", 512, lut)

    write("mikie_palette.h", palette)

    sizes = {"main": len(main), "audio": len(audio), "tiles": _TILE_COUNT * 32,
             "sprites": _SPRITE_COUNT * 128, "rgb": 512, "lut": 512}
    for k, v in sizes.items():
        ctx.log.info(f"  {k:8s} {v:6d}")
    total = sum(sizes.values())
    ctx.log.info(f"Arena total {total} B (budget 182000)")
    assert total < 182000


JOB = Job(name="mikie", convert=_convert, machine="mikie", zips=("mikie.zip",))
