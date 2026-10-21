# Vulgus (Capcom 1984), MAME capcom/vulgus.cpp, set vulgus.
# Display ROT270: every layer is stored per native x line so the render
# walks native y along a panel line (see vulgus.cpp).
# Chars: 8x8 2bpp, 512 codes, uint16_t[8]: word[nx] bits 2k = pixel at native y k.
# Tiles: 16x16 3bpp, 512 codes, uint32_t[16][2]: word[nx][h] nibble k = native y 8h+k.
# Sprites: 16x16 4bpp, 256 codes, same layout as tiles.
# Palette: 256 RGB565 (MAME 0x0e/0x1f/0x43/0x8f weights) + char, sprite
#   and tile lookups; color maps are built at runtime.
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.gfx.color import prom_level_4bit, rgb565_swapped
from romconv.gfx.gfxutil import mame_decode

_CHAR_COUNT, _TILE_COUNT, _SPRITE_COUNT = 512, 512, 256


def _lines16(gfx):
    out = []
    for t in gfx:
        for nx in range(16):
            for h in range(2):
                out.append(sum(t[8 * h + k][nx] << (4 * k) for k in range(8)))
    return out


def _convert(ctx: Context) -> None:
    def rom(name, sha1):
        return ctx.rom(name, sha1)

    main = (rom("vulgus.002", "48072aaa1f2603b6301d7542cc3df10ead2847bb")
            + rom("vulgus.003", "14dda82b90f9c3a309561a73c300cb54b5fca77d")
            + rom("vulgus.004", "f3f685955fc42f238909dcdb5edc4c117e5543db")
            + rom("vulgus.005", "6bc9dda7dbbbef82e9f61c9d5cf1555e5290b249")
            + rom("1-8n.bin", "6f28d143e984d3d6af3114702ec27d6e878cc35f"))
    audio = rom("1-11c.bin", "b58fb1ea7e30018102ee420d52a1597615412eb1")
    chars = rom("1-3d.bin", "c572b4a26f12013f5f6463b79ba9cbee4c474bbe")
    tiles = b"".join(rom(n, s) for n, s in [
        ("2-2a.bin", "f9f0d05475ae4c554552a71bc2f60e02b1442eb1"),
        ("2-3a.bin", "c4c633a909526308de4ad83e8ca449fa71eb3cb5"),
        ("2-4a.bin", "645666895127aededfa7872b20b7725948a9c462"),
        ("2-5a.bin", "c935176f8a9bce0f74ff466e10c23ff6557f85ec"),
        ("2-6a.bin", "987a4844c4568a088932f43a3aff847e6d6b4860"),
        ("2-7a.bin", "dbced07d4a886ed9ad3302aaa37bc02c599ee132")])
    sprites = b"".join(rom(n, s) for n, s in [
        ("2-2n.bin", "85bf67ce4d60b260767ba5fe9b9777f857937fe3"),
        ("2-3n.bin", "7b7df89398bf83ace1a8c216ca8526beae90972d"),
        ("2-4n.bin", "3f7bb4658d2126576a0f8f46f2c947eec1cd231a"),
        ("2-5n.bin", "8b69b9cd6db37db94a00da8712413055a631186a")])
    prom_r = rom("e8.bin", "218c1b404b4b5b06f06e04143872f6758f83f266")
    prom_g = rom("e9.bin", "d597097afc53fef752b2530d2de04e5aabb664b4")
    prom_b = rom("e10.bin", "c719892f0c6d8c82ee2ff41bfe74b67648f5b4f5")
    char_lut = rom("d1.bin", "6c1e8572a4c7b4825b89fc9549265be7c8f17788")
    spr_lut = rom("j2.bin", "7d76e1ff75466e190bc2e07ff3ffb45034f838cd")
    tile_lut = rom("c9.bin", "5a2110e97e82c087999ee4e5adf32d7fa06a3dfb")

    # --- chars: charlayout ---
    chars_dec = mame_decode(chars, 8, 8, [4, 0], [0, 1, 2, 3, 8, 9, 10, 11],
                            [16 * i for i in range(8)], 16 * 8, _CHAR_COUNT)
    char_words = []
    for t in chars_dec:
        for nx in range(8):
            char_words.append(sum(t[k][nx] << (2 * k) for k in range(8)))

    # --- tiles: tilelayout, 3 planes RGN_FRAC(n,3) ---
    third = len(tiles) * 8 // 3
    tiles_dec = mame_decode(tiles, 16, 16, [0, third, 2 * third],
                            list(range(8)) + [16 * 8 + i for i in range(8)],
                            [8 * i for i in range(16)], 32 * 8, _TILE_COUNT)

    # --- sprites: spritelayout ---
    half = len(sprites) * 8 // 2
    spr_dec = mame_decode(sprites, 16, 16, [half + 4, half, 4, 0],
                          [0, 1, 2, 3, 8, 9, 10, 11, 256, 257, 258, 259, 264, 265, 266, 267],
                          [16 * i for i in range(16)], 64 * 8, _SPRITE_COUNT)

    tile_words = _lines16(tiles_dec)
    spr_words = _lines16(spr_dec)

    rgb565 = [rgb565_swapped(prom_level_4bit(prom_r[i]), prom_level_4bit(prom_g[i]), prom_level_4bit(prom_b[i]))
              for i in range(256)]
    lut = list(char_lut) + list(spr_lut) + list(tile_lut)

    def write(name, fn):
        with ctx.header(name) as f:
            fn(f)
        ctx.log.info(f"wrote {name}")

    write("vulgus_rom_main.h",
          lambda f: emit_compressed(f, "vulgus_rom_main", "unsigned char", "", len(main), main))
    write("vulgus_rom_audio.h",
          lambda f: emit_compressed(f, "vulgus_rom_audio", "unsigned char", "", len(audio), audio))
    write("vulgus_chars.h",
          lambda f: emit_compressed(f, "vulgus_chars", "unsigned short", "[8]", _CHAR_COUNT, char_words))
    write("vulgus_tiles.h",
          lambda f: emit_compressed(f, "vulgus_tiles", "uint32_t", "[16][2]", _TILE_COUNT, tile_words))
    write("vulgus_sprites.h",
          lambda f: emit_compressed(f, "vulgus_sprites", "uint32_t", "[16][2]", _SPRITE_COUNT, spr_words))

    def palette(f):
        emit_compressed(f, "vulgus_rgb", "unsigned short", "", 256, rgb565)
        emit_compressed(f, "vulgus_lut", "unsigned char", "", 768, lut)

    write("vulgus_palette.h", palette)

    sizes = {"main": len(main), "audio": len(audio), "chars": _CHAR_COUNT * 16,
             "tiles": _TILE_COUNT * 128, "sprites": _SPRITE_COUNT * 128, "rgb": 512, "lut": 768}
    for k, v in sizes.items():
        ctx.log.info(f"  {k:8s} {v:6d}")
    total = sum(sizes.values())
    ctx.log.info(f"Arena total {total} B (budget 182000)")
    assert total < 182000


JOB = Job(name="vulgus", convert=_convert, machine="vulgus", zips=("vulgus.zip",))
