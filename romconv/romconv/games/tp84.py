# Time Pilot '84 (Konami 1984), MAME konami/tp84.cpp, set tp84.
# Tiles: 8x8 2bpp, stored rotated (row = native x, pixel k = native y 7-k),
#   2 bits per pixel LSB first in a uint16_t, for tp84's per-line bg render.
# Sprites: 16x16 4bpp native landscape [16][8], low nibble = even x; the
#   blit rotates and flips at runtime (circusc).
# Palette: 256 RGB565 from the 3 PROMs (MAME resnet weights) + char and
#   sprite lookups; the per-bank color maps are built at runtime.
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.emit.write import write_asset
from romconv.gfx.color import rgb565_swapped
from romconv.gfx.gfxutil import mame_decode, pack_pairs, pack_pens, rot_galagino
from romconv.gfx.resnet import combine, resistor_weights
from romconv.plan import layout
from romconv.plan.budget import log_sizes


def _convert(ctx: Context) -> None:
    arena_capacity = sum(layout.read_layouts(layout.header_path(ctx.paths))["local"])

    def rom(name, sha1):
        return ctx.rom(name, sha1)

    main = (rom("388_f04.7j", "6848ef35ec7f92cccefb0fb2de42c4b0e9ec476f")
            + rom("388_05.8j", "f3bb1ee66c9e47d050370ac9ca74f3020cb9cfa3")
            + rom("388_f06.9j", "65dee1fd4c940a5423d57cb55a7f2ad89c59c5c6")
            + rom("388_07.10j", "896e31c59aedf1c7e73e6f30fbe78cc020b457ab"))
    sub = rom("388_f08.10d", "118a1b46ee01a583e6cf39af59b073321c76dbff")
    audio = rom("388j13.6a", "981289f5bdf7dc1348f4ca547ac933ef503b6588")
    tiles = (rom("388_h02.2j", "1a3c7cd47ad34e37a7b0f3014e10c055cbb2b559")
             + rom("388_d01.1j", "6975f3a1603b14132aab58329195a4845a6e28bb"))
    sprites = (rom("388_e09.12a", "6f48d3efc53d63171ec655e64b225412de1374e4")
               + rom("388_e10.13a", "7e2dde080bb614709561431a81b0490b2aaa42a9")
               + rom("388_e11.14a", "792aaa4daedc8eb807d5a66d87da4641739b1660")
               + rom("388_e12.15a", "d90f99b19ab3cddfdfd37a273fb437be098088bc"))
    prom_r = rom("388d14.2c", "e39026f87f5b995cf4a38b5d3d3fee7561762ae6")
    prom_g = rom("388d15.2d", "f09d8b92c7f9bf046cdd815c5282d0510e61b6e0")
    prom_b = rom("388d16.1e", "9ba8af294dbd6f3a5d039c74a56e0605a913c037")
    char_lut = rom("388d18.1f", "3f74ad733b07b6a31cf9d4956d171eb9253dd6bf")
    spr_lut = rom("388j17.16c", "42ab23206be99e840bd9c52cefa175c12fac8e5b")

    # --- tiles: MAME charlayout, rotated for galagino ---
    tiles_dec = mame_decode(tiles, 8, 8, [4, 0], [0, 1, 2, 3, 64, 65, 66, 67],
                            [8 * i for i in range(8)], 128, 1024)
    # rot_galagino: r[nx][k] = t[7-k][nx]: row = native x, pixel k = native y 7-k
    tile_words = [pack_pens(row, 2) for t in tiles_dec for row in rot_galagino(t)]

    # --- sprites: MAME spritelayout, native orientation ---
    half = len(sprites) * 8 // 2
    spr_dec = mame_decode(sprites, 16, 16, [half + 4, half + 0, 4, 0],
                          [0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195],
                          [8 * i for i in range(8)] + [256 + 8 * i for i in range(8)], 512, 256)
    spr_bytes = pack_pairs(spr_dec)

    # --- palette: MAME compute_resistor_weights(0, 255, -1, 4, {1000,470,220,100}, w, 470, 0) ---
    (w,), _ = resistor_weights(0, 255, -1.0, [([1000, 470, 220, 100], 470, 0)])

    def comp(v):
        return combine(w, v & 1, (v >> 1) & 1, (v >> 2) & 1, (v >> 3) & 1)

    rgb888 = [(comp(prom_r[i]), comp(prom_g[i]), comp(prom_b[i])) for i in range(256)]
    # title screen colors from a MAME snapshot (frame 440): must be in the palette
    for c in [(224, 224, 224), (175, 175, 45)]:
        if c not in rgb888:
            near = sorted(set(rgb888), key=lambda p: sum((a - b) ** 2 for a, b in zip(p, c)))[:6]
            ctx.log.error(f"nearest to {c}: {near}")
        assert c in rgb888, f"palette mismatch: {c} not in computed palette"
    rgb565 = [rgb565_swapped(*c) for c in rgb888]
    lut = [b & 0x0F for b in char_lut] + [b & 0x0F for b in spr_lut]

    def write(name, fn):
        write_asset(ctx, name, None, fn)

    # uint16_t == unsigned short; emit_compressed knows the latter
    write("tp84_rom_main.h", lambda f: emit_compressed(f, "tp84_rom_main", "unsigned char", "", len(main), main))
    write("tp84_rom_sub.h", lambda f: emit_compressed(f, "tp84_rom_sub", "unsigned char", "", len(sub), sub))
    write("tp84_rom_audio.h",
          lambda f: emit_compressed(f, "tp84_rom_audio", "unsigned char", "", len(audio), audio))
    write("tp84_tiles.h", lambda f: emit_compressed(f, "tp84_tiles", "unsigned short", "[8]", 1024, tile_words))
    write("tp84_sprites.h",
          lambda f: emit_compressed(f, "tp84_sprites", "unsigned char", "[16][8]", 256, spr_bytes))

    def palette(f):
        emit_compressed(f, "tp84_rgb", "unsigned short", "", 256, rgb565)
        emit_compressed(f, "tp84_lut", "unsigned char", "", 512, lut)

    write("tp84_palette.h", palette)

    sizes = {"main": len(main), "sub": len(sub), "audio": len(audio),
             "tiles": 1024 * 16, "sprites": 256 * 128, "rgb": 512, "lut": 512}
    log_sizes(ctx, sizes, arena_capacity)


JOB = Job(name="tp84", convert=_convert, machine="tp84", zips=("tp84.zip",))
