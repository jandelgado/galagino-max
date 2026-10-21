"""Convert the MAME mario (US rev G) ROM set into galagino headers."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.emit.write import write_asset, write_bytes
from romconv.games.mario.discrete import samples
from romconv.games.mario.palette import rgb_palette
from romconv.gfx.adpcm import emit_adpcm
from romconv.gfx.color import rgb565_swapped
from romconv.gfx.gfxutil import mame_decode
from romconv.gfx.rom import rom_bytes, rom_bytes_all
from romconv.plan.budget import check_budget


# (names in the merged zip, offset in the image, SHA1) from MAME ROM_START( mario ).
# Main image: 0000-5fff, then f000-ffff stored at 0x6000.
_MAIN = (
    (("tma1-c-7f_g.7f", "tma1-c-7f_e-1.7f"), 0x0000, "36a04f9ca1c2a583477cb8a6f2ef94e044e08296"),
    (("tma1-c-7e_g.7e", "mariog/tma1-c-7e_g.7e"), 0x2000, "e372f846d0e5a2b9b47ebd0330293fcc8a12363f"),
    (("tma1-c-7d_g.7d", "tma1-c-7d_e-1.7d"), 0x4000, "b19804e69ce2c98cf276c6055c3a250316b96b45"),
    (("tma1-c-7c_g.7c", "mariof/tma1-c-7c_f.7c"), 0x6000, "b09060b2c84ab77cc540a27b8f932cb60ec8d442"),
)
_SOUND = (("tma1-c-6k_e.6k",), "111a29bcb9cda0d935675fa26eca6b099a88427f")
_CHARS = (
    (("tma1-v-3f.3f",), "46749568aff88a28c3b6a1ac423abd1b90742a4d"),
    (("tma1-v-3j.3j",), "15fae47d701dc1ef15c943cee6aa991776ecffdf"),
)
_SPRITES = (
    (("tma1-v-7m.7m", "mariof/tma1-v-7m.7m"), "4a1c1e239cb6d483e76f50d7a3b941025963c6a3"),
    (("tma1-v-7n.7n",), "0747d693b9482f6dd28b0bc484fd1d3e29d35654"),
    (("tma1-v-7p.7p",), "15a6e16c189d45f72761ebcbe9db5001bdecd659"),
    (("tma1-v-7s.7s", "mariof/tma1-v-7s.7s"), "9af6844dbaa3615433d0595e9e85e72493e31a54"),
    (("tma1-v-7t.7t",), "589fe108c7c11278fd897f2ded8f0498bc149cfd"),
    (("tma1-v-7u.7u", "mariof/tma1-v-7u.7u"), "d9194ff7b89a18273d37b47228fc7fb7e2a0ed1f"),
)
_PROM = (["tma1-c-4p.4p"], "90b739c4c7f24a88b6ac5ca29b06c032906a2801")


def _write(ctx, filename, description, emit):
    return write_asset(ctx, filename, description, emit, "mario")


def _chars(ctx):
    # charlayout: 512 chars 8x8 2bpp, planes { 512*8*8, 0 }
    tiles = mame_decode(rom_bytes_all(ctx, _CHARS), 8, 8, [512 * 64, 0], list(range(8)),
                        [8 * y for y in range(8)], 64, 512)
    # row word: pixel x in bits 2x..2x+1
    return [sum(p << (2 * x) for x, p in enumerate(row)) for t in tiles for row in t]


def _sprites(ctx):
    # spritelayout: 256 sprites 16x16 3bpp, x halves 256*16*8 bits apart
    half = 256 * 16 * 8
    xoffs = list(range(8)) + [half + x for x in range(8)]
    tiles = mame_decode(rom_bytes_all(ctx, _SPRITES), 16, 16, [2 * 256 * 256, 256 * 256, 0], xoffs,
                        [8 * y for y in range(16)], 128, 256)
    # SpriteRows4bpp: per row two words, pixels 0-7 and 8-15, pixel 0 low nibble
    words = []
    for t in tiles:
        for row in t:
            for h in (0, 8):
                words.append(sum(p << (4 * x) for x, p in enumerate(row[h:h + 8])))
    return words


def _palette(prom):
    return [rgb565_swapped(*c) for c in rgb_palette(prom)]


def _convert(ctx: Context) -> None:
    main_rom = bytearray(0x7000)
    for names, base, sha1 in _MAIN:
        rom = ctx.rom(names, sha1)
        main_rom[base:base + len(rom)] = rom
    sound = rom_bytes(ctx, _SOUND)
    assert len(sound) == 0x1000

    total = write_bytes(ctx, "mario_rom_main.h", "mario_rom_main", main_rom,
                        "Z80 ROM: 0x0000-0x5fff, then 0xf000-0xffff at 0x6000.", "mario")
    total += write_bytes(ctx, "mario_rom_sound.h", "mario_rom_sound", sound,
                         "M58715 ROM 6k as in the file (program 0x000-0xfff in EA mode).", "mario")

    words = _chars(ctx)
    total += _write(ctx, "mario_tiles.h", "512 chars 8x8 2bpp, uint16_t row, pixel x in bits 2x.",
                    lambda out: emit_compressed(out, "mario_tiles", "unsigned short", "[8]", 512, words)
                    or 2 * len(words))
    words = _sprites(ctx)
    total += _write(ctx, "mario_sprites.h", "256 sprites 16x16 3bpp in SpriteRows4bpp layout.",
                    lambda out: emit_compressed(out, "mario_sprites", "uint32_t", "[32]", 256, words)
                    or 4 * len(words))
    colors = _palette(rom_bytes(ctx, _PROM))
    total += _write(ctx, "mario_palette.h", "256 pens, RGB565 byte swapped.",
                    lambda out: emit_compressed(out, "mario_palette", "unsigned short", "", 256, colors) or 512)
    for name, pcm in samples().items():
        total += _write(ctx, f"mario_sample_{name}.h", f"{name} discrete sound, IMA-ADPCM 24 kHz.",
                        lambda out, name=name, pcm=pcm: emit_adpcm(out, f"mario_sample_{name}", pcm,
                                                                     compress=True))

    check_budget(ctx, total)


JOB = Job(name="mario", convert=_convert, machine="mario", zips=("mario.zip",))
