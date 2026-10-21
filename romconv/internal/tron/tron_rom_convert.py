#!/usr/bin/env python3
"""Convert MAME MCR 90010 ROM sets (tron, shollow) into galagino headers
(MAME midway/mcr.cpp, mcr_m.cpp, midway.cpp). Usage: tron_rom_convert.py [set].

Both games are ROT90 at 512x480 with 8x8 tiles doubled by the hardware. galagino shows
it at half resolution (240x256): tiles 1:1, 32x32 sprites downsampled to 16x16.
Both are stored rotated into panel orientation:

    native (x, y) in a tile  ->  panel row = x, panel column = size - 1 - y
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "pyconv"))
from gfxutil import load_file
from mcr_hw import decode_tiles_raw, decode_sprites_raw, duty_gain
from asset_emit import emit_compressed

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..", "..")
SSIO_ZIP = os.path.join(ROOT, "romszip", "midssio.zip")

# MAME ROM_START( tron ), set 1 (8/9), SHA1s from mame -listxml tron
TRON_MAIN = [
    ("scpu-pga_lctn-c2_tron_aug_9.c2", "378847604a6a9c079d887348010ab9539d5f6195"),
    ("scpu-pgb_lctn-c3_tron_aug_9.c3", "e0c294afa8ba0b0ba89e3e0fb3ff6d8fc4398e32"),
    ("scpu-pgc_lctn-c4_tron_aug_9.c4", "24c1bd2f31e194542571c391c5dccf21354115c2"),
    ("scpu-pgd_lctn-c5_tron_aug_9.c5", "1b827ca30a439d2f4cc94fcc0e90ee0cf87e018c"),
    ("scpu-pge_lctn-c6_tron_aug_9.c6", "45ac7c53f6f4eba5c7bf3fc6559cddd3821eddad"),
    ("scpu-pgf_lctn-c7_tron_aug_9.c7", "a7cd496ce75199b8279ea963520cf70d5f562bb2"),
]
TRON_SOUND = [
    ("ssi-0a_lctn-a7_tron.a7", "42efeefc8571dfc237c0be3368248f1e56add92e"),
    ("ssi-0b_lctn-a8_tron.a8", "0876e5eeaa63bb8cc97f3634a6ddd8a29a9b012f"),
    ("ssi-0c_lctn-a9_tron.a9", "ce8452a99a313ae7429de471bbea39de08c9fd4b"),
]
TRON_TILES = [
    ("scpu-bgg_lctn-g3_tron.g3", "b0d85b47873ac8ad475da18b9540d37232cb2b7c"),
    ("scpu-bgh_lctn-g4_tron.g4", "a38ea5f1db27f05d9689db838ce7a8de98f34837"),
]
TRON_SPRITES = [
    ("vga_lctn-e1_tron.e1", "c5d54d0b80ac768ccf6fdd32cad1ef6359fa324c"),
    ("vgb_lctn-dc1_tron.dc1", "5fb4268c9c73bdfc3b1e866618979aea3f219bbc"),
    ("vgc_lctn-cb1_tron.cb1", "11f4d744374e475d2c5b195a9f70888414529dd3"),
    ("vgd_lctn-a1_tron.a1", "71ed80ecd8caaf9fce1d7010f95c4678c9bd7102"),
]
# MAME ROM_START( shollow ), set 1, SHA1s from mame -listxml shollow
SHOLLOW_MAIN = [
    ("sh-pro.00", "8781295e21e5202288293fb9eb558cb1835b11ea"),
    ("sh-pro.01", "575cdba5bf9965c7765ce913a3376c7ba36b8291"),
    ("sh-pro.02", "ebae4711f124a34f19305da16657ece805e1da26"),
    ("sh-pro.03", "3ac6660d2b0166c58ec4fecfe37ce36e545064df"),
    ("sh-pro.04", "fd8ea76b3a7ffaf48fc11dd3b7c58e548e3e57c5"),
    ("sh-pro.05", "771e4c20d63e4e1d99723e6355db67064a278ae5"),
]
SHOLLOW_SOUND = [
    ("sh-snd.01", "b34f37fca61cdba26b5671feee991d133b8697a4"),
    ("sh-snd.02", "9b1a56962b2d210b1013bc35de780c6d5b3eb4bc"),
    ("sh-snd.03", "ba724f9cc0cc35dd31d4ad8b36a51da9d6cbfbcf"),
]
SHOLLOW_TILES = [
    ("sh-bg.00", "b4347d0b3d6149e94da4a38684c0fab931e76cc5"),
    ("sh-bg.01", "550e2e8c0bcbf7913b218fe48cd3324622c8d0f4"),
]
SHOLLOW_SPRITES = [
    ("sh-fg.00", "88dd803158d3c65429b79d2aaf8334130315aa93"),
    ("sh-fg.01", "cee5f0a66e40c779f1bfbb4bfafe5301385e7dae"),
    ("sh-fg.02", "71165df59dd5ca037116dc0f46fd4f6aac6dbfd1"),
    ("sh-fg.03", "82e006b01ee12390308be530d4b68fc2404b6b91"),
]

SETS = {
    "tron": (TRON_MAIN, TRON_SOUND, TRON_TILES, TRON_SPRITES),
    "shollow": (SHOLLOW_MAIN, SHOLLOW_SOUND, SHOLLOW_TILES, SHOLLOW_SPRITES),
}
SSIO_PROM = ("82s123.12d", "9ac9b01d24affc0ee9227a4364c4fd8f8290343a")

ARENA_BUDGET = 182000


def load(zip_path, entry):
    name, sha1 = entry
    return bytes(load_file(zip_path, [name], sha1))


def load_all(zip_path, entries):
    return b"".join(load(zip_path, e) for e in entries)


def pack_row(pens):
    # first pixel in the top nibble
    v = 0
    for p in pens:
        v = (v << 4) | p
    return v


def rotate(tile):
    # panel[row][col] = native[size - 1 - col][row]
    n = len(tile)
    return [[tile[n - 1 - c][r] for c in range(n)] for r in range(n)]


def decode_tiles(rom):
    tiles = decode_tiles_raw(rom)
    return len(tiles), [pack_row(row) for t in tiles for row in rotate(t)]


def downsample(sprite, px, py):
    # 2:1 per axis, pixel (2k + px, 2l + py) of each block. Sprites are ORed
    # together in hardware (dithered overlaps blend), so all sprites must
    # sample the same screen pixels, like a 2:1 nearest scale of MAME's frame.
    return [[sprite[2 * l + py][2 * k + px] for k in range(16)] for l in range(16)]


def decode_sprites(rom):
    # Panel pixels sample even native x, odd native y. Native flips swap the
    # sampled source parity, so 4 variants: index (py << 1 | px) * count + code,
    # px = hflip, py = !vflip.
    sprites = decode_sprites_raw(rom)
    count = len(sprites)
    flat = []
    for py in range(2):
        for px in range(2):
            for s in sprites:
                for row in rotate(downsample(s, px, py)):
                    flat += [pack_row(row[:8]), pack_row(row[8:])]
    return count * 4, flat


def header(path, comment):
    f = open(path, "w", newline="\n")
    f.write("// Generated by romconv/internal/tron/tron_rom_convert.py; do not edit.\n")
    f.write(f"// {comment}\n")
    return f


def emit(out_dir, name, ctype, dims, count, flat, comment):
    path = os.path.join(out_dir, f"{name}.h")
    with header(path, comment) as out:
        emit_compressed(out, name, ctype, dims, count, flat)
    print(f"  wrote {path}")


def main(game):
    main_files, sound_files, tile_files, sprite_files = SETS[game]
    zip_path = os.path.join(ROOT, "romszip", f"{game}.zip")
    out_dir = os.path.join(ROOT, "source", "src", "machines", game)
    os.makedirs(out_dir, exist_ok=True)
    main_rom = load_all(zip_path, main_files)
    sound_rom = load_all(zip_path, sound_files)
    tile_count, tiles = decode_tiles(load_all(zip_path, tile_files))
    sprite_count, sprites = decode_sprites(load_all(zip_path, sprite_files))
    gain = duty_gain(load(SSIO_ZIP, SSIO_PROM))

    emit(out_dir, f"{game}_rom", "unsigned char", "", len(main_rom), list(main_rom), "main Z80 ROM 0x0000-0xbfff")
    emit(out_dir, f"{game}_snd_rom", "unsigned char", "", len(sound_rom), list(sound_rom), "SSIO Z80 ROM 0x0000-0x2fff")
    emit(out_dir, f"{game}_tilemap", "uint32_t", "[8]", tile_count, tiles,
         f"{tile_count} tiles 8x8 4 bpp, rotated, row = 8 pens, first in bits 31-28")
    emit(out_dir, f"{game}_spritemap", "uint32_t", "[16][2]", sprite_count, sprites,
         f"{sprite_count // 4} sprites 32x32 downsampled to 16x16 4 bpp in 4 sampling variants, rotated, [row][0] pens 0-7, first in bits 31-28")

    path = os.path.join(out_dir, f"{game}_ssio_prom.h")
    with header(path, "SSIO AY voice gain / 16 per duty-cycle nibble (PROM 82s123.12d)") as f:
        f.write(f"static const uint8_t {game}_ssio_duty_gain[16] = {{ " + ", ".join(map(str, gain)) + " };\n")
    print(f"  wrote {path}: {gain}")

    sizes = {"main": len(main_rom), "sound": len(sound_rom),
             "tilemap": len(tiles) * 4, "spritemap": len(sprites) * 4}
    total = sum(sizes.values())
    print(f"  tiles {tile_count}, sprites {sprite_count}")
    print("  Arena budget: " + ", ".join(f"{k} {v}" for k, v in sizes.items())
          + f" = {total} / {ARENA_BUDGET} bytes")
    assert total < ARENA_BUDGET


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "tron")
