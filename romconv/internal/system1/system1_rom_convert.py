#!/usr/bin/env python3
"""Convert a Sega System 1 ROM set into galagino headers (MAME sega/system1.cpp)."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "pyconv"))
from gfxutil import load_file, rgb888_to_rgb565_le
from asset_emit import emit_compressed
from resnet import resistor_weights, combine

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..", "..")
MACHINES = os.path.join(ROOT, "source", "src", "machines")

ARENA_CAPACITY = 182000
ARENA_BLOCK = 91000
MIX_NO_COLLIDE = 0x04

# MAME ROM_START per game, regions in load order
GAMES = {
    "starjack": {
        "main": [("epr-5320b.129", "28d3f87851cccc94a86eb0217893de0baf8e62fd"),
                 ("epr-5321a.130", "79fd23bb7db577d1dbf1b50503085eccdd17b98c"),
                 ("epr-5322a.131", "6bb8a063279c93341ff472351b79c92795845f74"),
                 ("epr-5323.132", "6cfa288e28e3b415db5ef3cef8e8849259234af9"),
                 ("epr-5324.133", "d36ef8f176d5e44884d3d0b9af81be6f7fbd0cde"),
                 ("epr-5325.134", "27362b3e10d9d4235647eadb9cd1404700a8fb49")],
        "sound": [("epr-5332.3", "4a6ad09949a438562d7043532d628cefdbb5a2fe")],
        "tiles": [("epr-5331.82", "353067a75d583d5f53ce2f473b52a35dd912639f"),
                  ("epr-5330.65", "a2e90d20a07608f43273e84d1eb22f195b19626c"),
                  ("epr-5329.81", "6d19543427b9c4d8d8f5ea0872cdf8cc4fe5018c"),
                  ("epr-5328.64", "cc30d144ff70539bbc82c848c154e156a33b638c"),
                  ("epr-5327.80", "03124ce123684b8469cf42be6ed5f0fffa64c480"),
                  ("epr-5326.63", "bc7528456fe8dee9aa21212aa996fc347c5d55b4")],
        "sprites": [("epr-5318.86", "326d538551245fce67d0fdba85975e27093b7a93"),
                    ("epr-5319.93", "bb331be270dc1da63699533d9f02d73ce28f1bc5")],
        "lookup": [("pr-5317.106", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
    # regulusu: unencrypted set, files in subdir regulusu/ of regulus.zip
    "regulus": {
        "main": [("regulusu/epr-5950.129", "0164cb919a50013f23568f59caff19ff2d0bf11f"),
                 ("regulusu/epr-5951.130", "9e339c716c646bd02bedbe27096b75f633554e7c"),
                 ("regulusu/epr-5952.131", "9b6d8b2e0a0bec45bfbb9f8ccc728e18e909685f"),
                 ("regulusu/epr-5953.132", "1e051595aff34db06186542bcfc3849bc88eb5d4"),
                 ("epr-5644.133", "6fe471548d227d834c012d5d148b1ea1c12dfd00"),
                 ("regulusu/epr-5955.134", "4f94eaac900da5ca512289e2339776b1139e03e1")],
        "sound": [("epr-5652.3", "bc181c73a6009ca723e715650adb920b77bd311c")],
        "tiles": [("epr-5651.82", "f86acf2de639ac89f80cdf627d1d6b5f5e4f1557"),
                  ("epr-5650.65", "27ba8e2bb820913e58cb029da9c18d35e67728b8"),
                  ("epr-5649.81", "28f74bcf1e6bc06db0984dcf86dd527e301b0c01"),
                  ("epr-5648.64", "1ee18562250468f8f09a3062705422c28c740674"),
                  ("epr-5647.80", "2c1be04de6ec652ea8a566eb0eb1a9bcb4c90e66"),
                  ("epr-5646.63", "e34d1e1aaf3ae7a138e75df5dedebfb4acd79340")],
        "sprites": [("epr-5638.86", "c8024f541086a8a940e21219fa4522646aeb365a"),
                    ("epr-5639.93", "033bf46d2625f99544a784fe3fa299cc1b1b48e1")],
        "lookup": [("pr-5317.106", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
    # upndownu: unencrypted set, files in subdir upndownu/ of upndown.zip
    "upndown": {
        "main": [("upndownu/epr-5679.129", "7904ffb46a2c3ef69b9784f343ff37d81bbee11d"),
                 ("upndownu/epr-5680.130", "14cc846f03b71e0922689388a6757955cfd88bd8"),
                 ("upndownu/epr-5681.131", "440dc8c18183612c32486c617f5d7f38fd804f0e"),
                 ("upndownu/epr-5682.132", "71f06d1c4a580fed07ad32c6d1f2d37d47ed95b1"),
                 ("epr-5520.133", "eff0c91ce6c2c1f6e191bcbf9ae83dd377cbb408"),
                 ("upndownu/epr-5684.133", "ebe87d28dde6b8356d40572e9f2cd35ec240075f")],
        "sound": [("upndownu/epr-5528.3", "7f5385aa0773681329a4759b0fa6f975e3de6755")],
        "tiles": [("epr-5527.82", "c079136a5d73e1d55ddbad6efb5e7067d0ff412b"),
                  ("epr-5526.65", "db796d5c4ab3f749287133eaf05818f89dc2afb7"),
                  ("epr-5525.81", "2022cbd42ff0177cdd661bb00b1004459b6af83a"),
                  ("epr-5524.64", "6a9c909d10ccb03a8af6fa9d8067946d60b91592"),
                  ("epr-5523.80", "6c47fa433e16ccc3fff9347a4fe8f0165d20a3d2"),
                  ("epr-5522.63", "4f18274f5dc349b99b3daec517ccf5ccbb932d1c")],
        "sprites": [("epr-5514.86", "ca7db3df10deb6720096e6c50eddd9b74c47f0a0"),
                    ("epr-5515.93", "aedff8ce07ab16942037e5aff212652e51c19e71")],
        "lookup": [("pr-5317.106", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
    # raflesiau: unencrypted set, main ROMs in subdir raflesiau/ of raflesia.zip
    "raflesia": {
        "main": [("raflesiau/epr-7433.129", "4c02e052c01099478adb5bb595c035aff862dd45"),
                 ("raflesiau/epr-7434.130", "7bba493c8493611952e029dc8782916cc25db82a"),
                 ("raflesiau/epr-7435.131", "5bffdcdf543f47ccf2ed0cabf1ef9e42857d59f9"),
                 ("raflesiau/epr-7436.132", "aecf33c777e8f4a7323d72dd3f2ca5d12cc798e7"),
                 ("raflesiau/epr-7437.133", "a97654ff82ffa051ee14359733c2b9a952134d4e"),
                 ("raflesiau/epr-7438.134", "c353ed4d21661ea0721d8b85d6bfefe502373d7b")],
        "sound": [("epr-7420.120", "9cb18e3002c32f658e4725707069f9cd2f496507")],
        "tiles": [("epr-7419.62", "78c4d380d5558212e535c3262223137447d64818"),
                  ("epr-7418.61", "48be9337f704a11ac1fdeb64a3b3518c796bcdd0"),
                  ("epr-7417.64", "5cfd19241c54782c262bbb23c6f682534e77feb7"),
                  ("epr-7416.63", "78bb1c4651bd63a9f776766d2eac4f1c09242ed5"),
                  ("epr-7415.66", "b04ee35f603c6c9923ba888914eb43a8b7753d92"),
                  ("epr-7414.65", "bdc0185d133f7bbe287106882bacde846634ffa4")],
        "sprites": [("epr-7407.117", "c6f06144b708055b31fbcba9f38b63736db789d8"),
                    ("epr-7409.04", "e63f0422814423be91d8e1937a13d19693a1a5fc"),
                    ("epr-7408.110", "8476c2c01920f0492cf643929d4f023f3afe0164"),
                    ("epr-7410.05", "d0ad845bfe83412ac8d43125e1c50d0581a5b47e")],
        "lookup": [("pr-5317.76", "c7986aa9127ef5b50b845434cb4e81dff9861cd2")],
    },
}


def load(zip_path, files):
    return b"".join(bytes(load_file(zip_path, [name], sha1)) for name, sha1 in files)


def bit(v, n):
    return (v >> n) & 1


# charlayout: 3 planes RGN_FRAC(n,3), plane 0 = MSB, byte bit 7 = left pixel.
# Emitted column-major: word per native pixel column, native row 0 in bits 23..21.
def decode_tiles(rom):
    plane = len(rom) // 3
    count = plane // 8
    assert count & (count - 1) == 0, "tile count must be a power of two (code mask)"
    out = []
    for c in range(count):
        for col in range(8):
            w = 0
            for row in range(8):
                px = 0
                for p in range(3):
                    px = (px << 1) | bit(rom[p * plane + c * 8 + row], 7 - col)
                w = (w << 3) | px
            out.append(w)
    return out, count


# Unique tiles in first-use order plus code -> unique index map (Arena budget)
def dedupe_tiles(words):
    index = {}
    unique = []
    tile_map = []
    for c in range(len(words) // 8):
        key = tuple(words[c * 8:c * 8 + 8])
        if key not in index:
            index[key] = len(index)
            unique.extend(key)
        tile_map.append(index[key])
    return unique, len(index), tile_map


def palette565():
    rg, b = [995, 495, 250], [495, 250]
    (wr, wg, wb), _ = resistor_weights(0, 255, -1.0, [(rg, 0, 0), (rg, 0, 0), (b, 0, 0)])
    return [rgb888_to_rgb565_le(combine(wr, bit(i, 0), bit(i, 1), bit(i, 2)),
                                combine(wg, bit(i, 3), bit(i, 4), bit(i, 5)),
                                combine(wb, bit(i, 6), bit(i, 7))) for i in range(256)]


def write_palette():
    path = os.path.join(MACHINES, "system1", "system1_palette.h")
    vals = palette565()
    with open(path, "w", newline="\n") as f:
        f.write("// Generated by romconv/internal/system1/system1_rom_convert.py; do not edit.\n")
        f.write("// System 1 palette RAM byte (BBGGGRRR) -> RGB565 byte-swapped.\n")
        f.write("static const unsigned short system1_palette[256] = {\n")
        for i in range(0, 256, 8):
            f.write("  " + ", ".join(f"0x{v:04X}" for v in vals[i:i + 8]) + ",\n")
        f.write("};\n")


def emit(game, asset, ctype, dims, count, values):
    path = os.path.join(MACHINES, game, f"{game}_{asset}.h")
    with open(path, "w", newline="\n") as f:
        emit_compressed(f, f"{game}_{asset}", ctype, dims, count, values)


def main(game):
    spec = GAMES[game]
    zip_path = os.path.join(ROOT, "romszip", f"{game}.zip")
    main_rom = load(zip_path, spec["main"])
    sound = load(zip_path, spec["sound"])
    tiles, tile_count = decode_tiles(load(zip_path, spec["tiles"]))
    tiles, unique_count, tile_map = dedupe_tiles(tiles)
    sprites = load(zip_path, spec["sprites"])
    lookup = load(zip_path, spec["lookup"])
    assert len(sprites) % 0x8000 == 0, "sprite ROM is banked in 0x8000 units"
    # the mixer only collides under opaque sprite pixels (collision engine relies on it)
    assert all(lookup[i] & MIX_NO_COLLIDE for i in range(1, 128, 2))

    emit(game, "main_rom", "unsigned char", "", len(main_rom), main_rom)
    emit(game, "sound_rom", "unsigned char", "", len(sound), sound)
    emit(game, "tiles", "uint32_t", "[8]", unique_count, tiles)
    emit(game, "tile_map", "unsigned short", "", tile_count, tile_map)
    emit(game, "sprites", "unsigned char", "", len(sprites), sprites)
    emit(game, "lookup_prom", "unsigned char", "", len(lookup), lookup)
    write_palette()

    sizes = sorted([len(tiles) * 4, len(tile_map) * 2, len(main_rom), len(sprites), len(sound), len(lookup)],
                   reverse=True)
    blocks = [0, 0]
    for s in sizes:
        i = 0 if blocks[0] + s <= ARENA_BLOCK else 1
        blocks[i] += s
        assert blocks[i] <= ARENA_BLOCK, f"{game}: asset {s} does not fit an Arena block"
    print(f"{game}: Arena {sum(sizes)} / {ARENA_CAPACITY} (blocks {blocks[0]}, {blocks[1]})")


if __name__ == "__main__":
    main(sys.argv[1])
