"""Midway MCR games: tapper, tron, shollow (Satan's Hollow), dotron (Discs of Tron)
(MAME midway/mcr.cpp, mcr_m.cpp, midway.cpp).

All run at half resolution: tiles 1:1, 32x32 sprites downsampled to 16x16, in the sampling
variants (flips) the render needs. The decoders are in romconv.hw.mcr.

  tapper   512x480 ROT0 (tappera set). Render maps native (x, y) to panel column x - 8, row y + 24.
           romszip/tapper.zip holds the tappera program ROMs and the shared graphics and sound
           ROMs under their old names. The sprite variant is the X flip, the game never sets Y flip.
  tron     ROT90, stored rotated into panel orientation:
           native (x, y) in a tile  ->  panel row = x, panel column = size - 1 - y
  dotron   ORIENTATION_FLIP_X, stored mirrored in X as the player sees it; render maps native
           (x, y) to panel column 247 - x, row y + 24."""
from collections.abc import Sequence
from functools import partial

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.write import write_array
from romconv.gfx.rom import rom_bytes_all
from romconv.hw.mcr import decode_sprites_raw, decode_tiles_raw, duty_gain
from romconv.plan.budget import log_sizes

_SSIO_ZIP = "midssio.zip"
_SSIO_PROM = ("82s123.12d", "9ac9b01d24affc0ee9227a4364c4fd8f8290343a")

# MAME ROM_START( tron ), set 1 (8/9), SHA1s from mame -listxml tron
_TRON_MAIN = (
    ("scpu-pga_lctn-c2_tron_aug_9.c2", "378847604a6a9c079d887348010ab9539d5f6195"),
    ("scpu-pgb_lctn-c3_tron_aug_9.c3", "e0c294afa8ba0b0ba89e3e0fb3ff6d8fc4398e32"),
    ("scpu-pgc_lctn-c4_tron_aug_9.c4", "24c1bd2f31e194542571c391c5dccf21354115c2"),
    ("scpu-pgd_lctn-c5_tron_aug_9.c5", "1b827ca30a439d2f4cc94fcc0e90ee0cf87e018c"),
    ("scpu-pge_lctn-c6_tron_aug_9.c6", "45ac7c53f6f4eba5c7bf3fc6559cddd3821eddad"),
    ("scpu-pgf_lctn-c7_tron_aug_9.c7", "a7cd496ce75199b8279ea963520cf70d5f562bb2"),
)
_TRON_SOUND = (
    ("ssi-0a_lctn-a7_tron.a7", "42efeefc8571dfc237c0be3368248f1e56add92e"),
    ("ssi-0b_lctn-a8_tron.a8", "0876e5eeaa63bb8cc97f3634a6ddd8a29a9b012f"),
    ("ssi-0c_lctn-a9_tron.a9", "ce8452a99a313ae7429de471bbea39de08c9fd4b"),
)
_TRON_TILES = (
    ("scpu-bgg_lctn-g3_tron.g3", "b0d85b47873ac8ad475da18b9540d37232cb2b7c"),
    ("scpu-bgh_lctn-g4_tron.g4", "a38ea5f1db27f05d9689db838ce7a8de98f34837"),
)
_TRON_SPRITES = (
    ("vga_lctn-e1_tron.e1", "c5d54d0b80ac768ccf6fdd32cad1ef6359fa324c"),
    ("vgb_lctn-dc1_tron.dc1", "5fb4268c9c73bdfc3b1e866618979aea3f219bbc"),
    ("vgc_lctn-cb1_tron.cb1", "11f4d744374e475d2c5b195a9f70888414529dd3"),
    ("vgd_lctn-a1_tron.a1", "71ed80ecd8caaf9fce1d7010f95c4678c9bd7102"),
)
# MAME ROM_START( shollow ), set 1, SHA1s from mame -listxml shollow
_SHOLLOW_MAIN = (
    ("sh-pro.00", "8781295e21e5202288293fb9eb558cb1835b11ea"),
    ("sh-pro.01", "575cdba5bf9965c7765ce913a3376c7ba36b8291"),
    ("sh-pro.02", "ebae4711f124a34f19305da16657ece805e1da26"),
    ("sh-pro.03", "3ac6660d2b0166c58ec4fecfe37ce36e545064df"),
    ("sh-pro.04", "fd8ea76b3a7ffaf48fc11dd3b7c58e548e3e57c5"),
    ("sh-pro.05", "771e4c20d63e4e1d99723e6355db67064a278ae5"),
)
_SHOLLOW_SOUND = (
    ("sh-snd.01", "b34f37fca61cdba26b5671feee991d133b8697a4"),
    ("sh-snd.02", "9b1a56962b2d210b1013bc35de780c6d5b3eb4bc"),
    ("sh-snd.03", "ba724f9cc0cc35dd31d4ad8b36a51da9d6cbfbcf"),
)
_SHOLLOW_TILES = (
    ("sh-bg.00", "b4347d0b3d6149e94da4a38684c0fab931e76cc5"),
    ("sh-bg.01", "550e2e8c0bcbf7913b218fe48cd3324622c8d0f4"),
)
_SHOLLOW_SPRITES = (
    ("sh-fg.00", "88dd803158d3c65429b79d2aaf8334130315aa93"),
    ("sh-fg.01", "cee5f0a66e40c779f1bfbb4bfafe5301385e7dae"),
    ("sh-fg.02", "71165df59dd5ca037116dc0f46fd4f6aac6dbfd1"),
    ("sh-fg.03", "82e006b01ee12390308be530d4b68fc2404b6b91"),
)

# MAME ROM_START( dotron ), upright 10/4/83
_DOTRON_MAIN = (
    ("disc_tron_uprt_pg0_10-4-83.1c", "e06a8097f02b9f445df0dd5c0ec13f9a0a1dcd8a"),
    ("disc_tron_uprt_pg1_10-4-83.2c", "8a1f088de9289cd902e72b55d3e72c3f07246778"),
    ("disc_tron_uprt_pg2_10-4-83.3c", "c773a68891fbf94808a2ee0036928c0c48d6673d"),
    ("disc_tron_uprt_pg3_10-4-83.4c", "9f861f99cb170513b68aee48bbfd60ee439d7fa9"),
)
_DOTRON_SOUND = (
    ("disc_tron_uprt_snd0_10-4-83.a7", "6d538a3e48f98e269623850f1f6774848a89fd59"),
    ("disc_tron_uprt_snd1_10-4-83.a8", "5c9a64604252eea0628bf9d6221e8add82f66abe"),
    ("disc_tron_uprt_snd2_9-22-83.a9", "261b0463a73b403bc46df3e04f3d12173787d6e7"),
    ("disc_tron_uprt_snd3_9-22-83.a10", "039d8d664f067bc0d085ad7730ef63dbd6dc387e"),
)
_DOTRON_TILES = (
    ("loc-bg2.6f", "782c8192dd58a3f23ff2338452dd03206d79030a"),
    ("loc-bg1.5f", "8044be9ffca9520fd77e0da492147e553f9f7da3"),
)
_DOTRON_SPRITES = (
    ("loc-g.cp4", "b97539ffd2f5fc8b86fc2f8f233cc26ba16f82ee"),
    ("loc-h.cp3", "3795ba1640790041da51ebeac8517cc7d32e243e"),
    ("loc-e.cp6", "24177a8dd6dcb377cf8aee7c7b47b26f29e77e20"),
    ("loc-f.cp5", "b7fb3d1df9b80c056cf131574565addb529645e1"),
    ("loc-c.cp8", "6cd83909b4376abce287e435a10e5bc25e18b265"),
    ("loc-d.cp7", "74983c922eae1326ecd0ff14000851e0b424cc61"),
    ("loc-a.cp0", "3f330ffde52ac57c02dfdf8e105aefcc10f87a0b"),
    ("loc-b.cp9", "9dfafd58bd552bfda4e1799a175735ecc1369ba3"),
)

# MAME ROM_START( tappera )
_TAPPER_MAIN = (
    ("tappg0.bin", "373e9a9d73b71e100c02862662d025f5ead2f94d"),
    ("tappg1.bin", "e493e46fc70a765e54bfdd7ba7ca570e6a5c79d6"),
    ("tappg2.bin", "cb46a2248289ced7282b1463f433dcb970c42c1a"),
    ("tappg3.bin", "45bf1571a2418c7dc00ccc7061a3e04e65cb6bff"),
)
_TAPPER_SOUND = (
    ("tapsnda7.bin", "9e281c340b7702523c86d56317efad9e3688e585"),
    ("tapsnda8.bin", "14334b9d2bfece3fe5bda0cbd53158ead8d27e53"),
    ("tapsnda9.bin", "b38bba5f12516d899e023f99147868e3402fbd7b"),
    ("tapsda10.bin", "0011407c1e886071282808c0a561789b1245a789"),
)
_TAPPER_TILES = (
    ("tapbg1.bin", "eb30b9bb654324340f0fc5b44776ac2440c1e869"),
    ("tapbg0.bin", "23e29ec942e1e7516ae8068837af2d1c79592378"),
)
_TAPPER_SPRITES = (
    ("tapfg1.bin", "a38667573d235efe2dc515e52af05825fe4e0f30"),
    ("tapfg0.bin", "2077f79177fda26f9c674b2ab525ec3833802059"),
    ("tapfg3.bin", "930142dd73fb30c4d3ec09a1e37517c6c6774024"),
    ("tapfg2.bin", "d553b8517c1d03a2be0b065f4da2fa99d9e6fb30"),
    ("tapfg5.bin", "8aead89e1adaee5f0b679661c4bfba36e0d639e8"),
    ("tapfg4.bin", "402c166d50b4a693959b3f0706a7931a5daef6ce"),
    ("tapfg7.bin", "95879a455ecfe2e3de7fe2a75078f9e6934960f5"),
    ("tapfg6.bin", "a24696f16d467d9eea4f25aef5f4c5ff55bf51ff"),
)


def _pack_row(pens: Sequence[int]) -> int:
    # first pixel in the top nibble
    v = 0
    for p in pens:
        v = (v << 4) | p
    return v


def _rotate(tile):
    # panel[row][col] = native[size - 1 - col][row]
    n = len(tile)
    return [[tile[n - 1 - c][r] for c in range(n)] for r in range(n)]


def _mirror(grid):
    # ORIENTATION_FLIP_X: the player sees native X mirrored
    return [row[::-1] for row in grid]


def _downsample(sprite, px, py):
    # 2:1 per axis, pixel (2k + px, 2l + py) of each block: what a 2:1 nearest scale of MAME's
    # view shows. Sprites are ORed together in hardware (dithered overlaps blend), so all
    # sprites must sample the same screen pixels.
    return [[sprite[2 * l + py][2 * k + px] for k in range(16)] for l in range(16)]


def _emit(ctx: Context, script: str, name: str, ctype: str, dims: str, count: int, flat: Sequence[int],
          comment: str) -> None:
    write_array(ctx, f"{name}.h", name, ctype, dims, count, flat, comment, script)


def _emit_gain(ctx: Context, script: str, game: str, comment: str, gain: Sequence[int]) -> None:
    with ctx.header(f"{game}_ssio_prom.h") as f:
        f.write(f"// Generated by romconv/internal/{script}/{script}_rom_convert.py; do not edit.\n")
        f.write(f"// {comment}\n")
        f.write(f"static const uint8_t {game}_ssio_duty_gain[16] = {{ " + ", ".join(map(str, gain)) + " };\n")
    ctx.log.info(f"  wrote {game}_ssio_prom.h: {gain}")


def _check_budget(ctx: Context, tile_count: int, sprite_count: int, sizes: dict[str, int]) -> None:
    ctx.log.info(f"  tiles {tile_count}, sprites {sprite_count}")
    log_sizes(ctx, sizes)


def _convert_tapper(ctx: Context) -> None:
    main_rom = rom_bytes_all(ctx, _TAPPER_MAIN)
    sound_rom = rom_bytes_all(ctx, _TAPPER_SOUND)
    tiles = decode_tiles_raw(rom_bytes_all(ctx, _TAPPER_TILES))
    sprites = decode_sprites_raw(rom_bytes_all(ctx, _TAPPER_SPRITES))
    gain = duty_gain(ctx.rom(*_SSIO_PROM, zip=_SSIO_ZIP))

    tile_rows = [_pack_row(row) for t in tiles for row in t]
    # 2 sampling variants: index flip_x * count + code
    sprite_rows = []
    for flip_x in range(2):
        for s in sprites:
            for row in _downsample(s, flip_x, 0):
                sprite_rows += [_pack_row(row[:8]), _pack_row(row[8:])]

    emit = partial(_emit, ctx, "tapper")
    emit("tapper_rom", "unsigned char", "", len(main_rom), list(main_rom), "main Z80 ROM 0x0000-0xdfff")
    emit("tapper_snd_rom", "unsigned char", "", len(sound_rom), list(sound_rom),
         "SSIO Z80 ROM 0x0000-0x3fff")
    emit("tapper_tilemap", "uint32_t", "[8]", len(tiles), tile_rows,
         f"{len(tiles)} tiles 8x8 4 bpp, row = 8 pens, first in bits 31-28")
    emit("tapper_spritemap", "uint32_t", "[16][2]", len(sprites) * 2, sprite_rows,
         f"{len(sprites)} sprites 32x32 downsampled to 16x16 4 bpp in 2 X flip variants, "
         "[row][0] pens 0-7, first in bits 31-28")
    _emit_gain(ctx, "tapper", "tapper", "SSIO AY voice gain / 16 per duty-cycle nibble (PROM 82s123.12d)", gain)

    sizes = {"main": len(main_rom), "sound": len(sound_rom),
             "tilemap": len(tile_rows) * 4, "spritemap": len(sprite_rows) * 4}
    _check_budget(ctx, len(tiles), len(sprites), sizes)


def _convert_tron(ctx: Context, game: str, files) -> None:
    main_files, sound_files, tile_files, sprite_files = files
    main_rom = rom_bytes_all(ctx, main_files)
    sound_rom = rom_bytes_all(ctx, sound_files)
    raw_tiles = decode_tiles_raw(rom_bytes_all(ctx, tile_files))
    tile_count = len(raw_tiles)
    tiles = [_pack_row(row) for t in raw_tiles for row in _rotate(t)]
    # Panel pixels sample even native x, odd native y. Native flips swap the sampled source
    # parity, so 4 variants: index (py << 1 | px) * count + code, px = hflip, py = !vflip.
    raw_sprites = decode_sprites_raw(rom_bytes_all(ctx, sprite_files))
    sprite_count = len(raw_sprites) * 4
    sprites = []
    for py in range(2):
        for px in range(2):
            for s in raw_sprites:
                for row in _rotate(_downsample(s, px, py)):
                    sprites += [_pack_row(row[:8]), _pack_row(row[8:])]
    gain = duty_gain(ctx.rom(*_SSIO_PROM, zip=_SSIO_ZIP))

    emit = partial(_emit, ctx, "tron")
    emit(f"{game}_rom", "unsigned char", "", len(main_rom), list(main_rom), "main Z80 ROM 0x0000-0xbfff")
    emit(f"{game}_snd_rom", "unsigned char", "", len(sound_rom), list(sound_rom), "SSIO Z80 ROM 0x0000-0x2fff")
    emit(f"{game}_tilemap", "uint32_t", "[8]", tile_count, tiles,
         f"{tile_count} tiles 8x8 4 bpp, rotated, row = 8 pens, first in bits 31-28")
    emit(f"{game}_spritemap", "uint32_t", "[16][2]", sprite_count, sprites,
         f"{sprite_count // 4} sprites 32x32 downsampled to 16x16 4 bpp in 4 sampling variants, rotated, "
         "[row][0] pens 0-7, first in bits 31-28")
    _emit_gain(ctx, "tron", game, "SSIO AY voice gain / 16 per duty-cycle nibble (PROM 82s123.12d)", gain)

    sizes = {"main": len(main_rom), "sound": len(sound_rom),
             "tilemap": len(tiles) * 4, "spritemap": len(sprites) * 4}
    _check_budget(ctx, tile_count, sprite_count, sizes)


def _convert_dotron(ctx: Context) -> None:
    main_rom = rom_bytes_all(ctx, _DOTRON_MAIN)
    sound_rom = rom_bytes_all(ctx, _DOTRON_SOUND)
    tiles = decode_tiles_raw(rom_bytes_all(ctx, _DOTRON_TILES))
    sprites = decode_sprites_raw(rom_bytes_all(ctx, _DOTRON_SPRITES))
    gain = duty_gain(ctx.rom(*_SSIO_PROM, zip=_SSIO_ZIP))

    tile_rows = [_pack_row(row) for t in tiles for row in _mirror(t)]
    # 4 sampling variants: index (flip_y << 1 | flip_x) * count + code. The view is mirrored, so
    # it samples odd native columns and even rows; native flips swap that parity.
    sprite_rows = []
    for flip_y in range(2):
        for flip_x in range(2):
            for s in sprites:
                for row in _mirror(_downsample(s, 1 - flip_x, flip_y)):
                    sprite_rows += [_pack_row(row[:8]), _pack_row(row[8:])]

    emit = partial(_emit, ctx, "dotron")
    emit("dotron_rom", "unsigned char", "", len(main_rom), list(main_rom), "main Z80 ROM 0x0000-0xdfff")
    emit("dotron_snd_rom", "unsigned char", "", len(sound_rom), list(sound_rom),
         "SSIO Z80 ROM 0x0000-0x3fff")
    emit("dotron_tilemap", "uint32_t", "[8]", len(tiles), tile_rows,
         f"{len(tiles)} tiles 8x8 4 bpp, mirrored in X, row = 8 pens, first in bits 31-28")
    emit("dotron_spritemap", "uint32_t", "[16][2]", len(sprites) * 4, sprite_rows,
         f"{len(sprites)} sprites 32x32 downsampled to 16x16 4 bpp in 4 flip variants, mirrored in X, "
         "[row][0] pens 0-7, first in bits 31-28")
    _emit_gain(ctx, "dotron", "dotron", "SSIO AY voice gain / 16 per duty-cycle nibble (PROM 82s123.12d)", gain)

    sizes = {"main": len(main_rom), "sound": len(sound_rom),
             "tilemap": len(tile_rows) * 4, "spritemap": len(sprite_rows) * 4}
    _check_budget(ctx, len(tiles), len(sprites), sizes)


JOBS = (
    # tapper is planned (arena plan)
    Job(name="tapper", convert=_convert_tapper, machine="tapper", zips=("tapper.zip", _SSIO_ZIP), plan=True),
    Job(name="tron", convert=partial(_convert_tron, game="tron",
                                      files=(_TRON_MAIN, _TRON_SOUND, _TRON_TILES, _TRON_SPRITES)),
        machine="tron", zips=("tron.zip", _SSIO_ZIP)),
    Job(name="shollow", convert=partial(_convert_tron, game="shollow",
                                         files=(_SHOLLOW_MAIN, _SHOLLOW_SOUND, _SHOLLOW_TILES,
                                                _SHOLLOW_SPRITES)),
        machine="shollow", zips=("shollow.zip", _SSIO_ZIP)),
    Job(name="dotron", convert=_convert_dotron, machine="dotron", zips=("dotron.zip", _SSIO_ZIP)),
)
