#!/usr/bin/env python3
"""Convert the MAME mpatrol ROM set into galagino headers (MAME irem/m52.cpp, m52_v.cpp)."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "pyconv"))
from gfxutil import load_file, mame_decode, rgb888_to_rgb565_le
from asset_emit import emit_compressed

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..", "..")
ZIP = os.path.join(ROOT, "romszip", "mpatrol.zip")
OUT_DIR = os.path.join(ROOT, "source", "src", "machines", "mpatrol")

# MAME ROM_START( mpatrol )
MAIN = [
    ("mpa-1.3m", "8c03726d6e049c3edbc277440184e31679f78258"),
    ("mpa-2.3l", "dfb2efb57378a20af6f20569f4360cde95596f93"),
    ("mpa-3.3k", "112c3c9678db8a8540a8df3708020c87fd10c91b"),
    ("mpa-4.3j", "727961b0dafa4a96b580d51013336db2a18aff1e"),
]
SOUND = ("mp-s1.1a", "4998c68a9e9a8002251fa8f07aa1082444a9dc80")
TX = [("mpe-5.3e", "b03d0d56150d3e9da4a4c871338097b4f450b649"),
      ("mpe-4.3f", "fecb3059fb09897a096add9452b50aec55c07545")]
SP = [("mpb-2.3m", "93c682e13e74bce29ced3a87bffb29569c114c3b"),
      ("mpb-1.3n", "1393ef92ae1ad58a4b62ca1660c0793d30a8b5e2")]
BG = [("mpe-1.3l", "8bb7c9acaf6833fb6c0575b015991b873a305a84"),   # mountains
      ("mpe-2.3k", "14c6c76e1d0db2c0745e5d6d33ea6945fac8e9ee"),   # hills
      ("mpe-3.3h", "8a090cb8d483a3d67c7360058e3fdd70e151cd62")]   # city
TX_PAL = ("mpc-4.2a", "dfc52958f2520e1ce4446dd4c84c91413bbacf76")
BG_PAL = ("mpc-3.1m", "2d1c12dab5915da2ccd466e39436c88be434d634")
SPR_PAL = ("mpc-1.1f", "8c41a8cce4f3384c392a9f7a223a50d7be0e14a5")
SPR_CLUT = ("mpc-2.2h", "bc0662fac82ffe65f02092d912b2c2b0c7a8ac2b")

# m52_v.cpp init_palette: pens per background image (indices into bg_pal)
BG_PENS = [[0, 4, 8, 12], [0, 1, 2, 3], [0, 17, 18, 19]]

ARENA_CAPACITY = 182000


def load(entry):
    name, sha1 = entry
    return bytes(load_file(ZIP, [name], sha1))


def step(start, inc, n):
    return [start + i * inc for i in range(n)]


# MAME video/resnet.cpp compute_resistor_weights; nets = [(resistances, pulldown, pullup)]
def resistor_weights(minval, maxval, scaler, nets):
    w = []
    for res, pd, pu in nets:
        ws = []
        for n in range(len(res)):
            r0 = 1e-12 if pd == 0 else 1.0 / pd
            r1 = 1e-12 if pu == 0 else 1.0 / pu
            for j, rj in enumerate(res):
                if j == n:
                    r1 += 1.0 / rj
                else:
                    r0 += 1.0 / rj
            r0, r1 = 1.0 / r0, 1.0 / r1
            vout = (maxval - minval) * r0 / (r1 + r0) + minval
            ws.append(min(max(vout, minval), maxval))
        w.append(ws)
    scale = scaler if scaler >= 0.0 else maxval / max(sum(ws) for ws in w)
    return [[x * scale for x in ws] for ws in w], scale


def combine(weights, *bits):
    return int(sum(wt * b for wt, b in zip(weights, bits)) + 0.5)


def bit(v, n):
    return (v >> n) & 1


def palettes(tx_pal, bg_pal, spr_pal):
    res3, res2 = [1000, 470, 220], [470, 220]
    (wr, wg, wb), scale = resistor_weights(0, 255, -1.0, [(res3, 0, 0), (res3, 0, 0), (res2, 0, 0)])

    def txbg(v):
        return (combine(wr, bit(v, 0), bit(v, 1), bit(v, 2)),
                combine(wg, bit(v, 3), bit(v, 4), bit(v, 5)),
                combine(wb, bit(v, 6), bit(v, 7)))

    (sr, sg, sb), _ = resistor_weights(0, 255, scale, [(res2, 470, 0), (res3, 470, 0), (res3, 470, 0)])

    def spr(v):
        return (combine(sr, bit(v, 6), bit(v, 7)),
                combine(sg, bit(v, 3), bit(v, 4), bit(v, 5)),
                combine(sb, bit(v, 0), bit(v, 1), bit(v, 2)))

    return [txbg(v) for v in tx_pal], [txbg(v) for v in bg_pal], [spr(v) for v in spr_pal]


def decode_chars(tx):
    tiles = mame_decode(tx, 8, 8, [0, 0x1000 * 8], step(0, 1, 8), step(0, 8, 8), 64, 512)
    flat = []
    for t in tiles:
        for row in t:
            v = 0
            for px in row:
                v = (v << 2) | px
            flat.append(v)
    return flat


def decode_sprites(sp):
    xoffs = step(0, 1, 8) + step(16 * 8, 1, 8)
    sprites = mame_decode(sp, 16, 16, [0x2000 * 8, 0, 0x1000 * 8], xoffs, step(0, 8, 16), 256, 128)
    flat = []
    for s in sprites:
        for row in s:
            v = 0
            for px in row:
                assert px < 4, "sprite plane 2 must be empty"
                v = (v << 2) | px
            flat.append(v)
    return flat


def decode_bg(rom):
    region = rom + b"\xff" * 0x1000
    xoffs = []
    for k in range(64):
        xoffs += step(8 * k, 1, 4)
    yoffs = step(0, 0x200, 32) + step(0x4000, 0x200, 32) + step(0x8000, 0x200, 32) + step(0xc000, 0x200, 32)
    img = mame_decode(region, 256, 128, [4, 0], xoffs, yoffs, 0, 1)[0]
    # Rows 64..127 come from the 0xff fill: solid pen 3 (render relies on it).
    assert all(px == 3 for row in img[64:] for px in row)
    out = []
    for row in img[:64]:
        for x in range(0, 256, 4):
            out.append((row[x] << 6) | (row[x + 1] << 4) | (row[x + 2] << 2) | row[x + 3])
    return out


def c565(rgb):
    return rgb888_to_rgb565_le(*rgb)


def write_cmap(path, tx_rgb, bg_rgb, spr_rgb, clut):
    with open(path, "w", newline="\n") as f:
        f.write("// Generated by romconv/internal/mpatrol/mpatrol_rom_convert.py; do not edit.\n")
        f.write("// Moon Patrol color maps, RGB565 byte-swapped for the SPI display.\n\n")
        f.write("// Characters: 128 colors x 4 pens (tx_pal direct)\n")
        f.write("const unsigned short mpatrol_tx_cmap[128][4] PROGMEM = {\n")
        for c in range(128):
            f.write("  { " + ", ".join(f"0x{c565(tx_rgb[c * 4 + p]):04x}" for p in range(4)) + " },\n")
        f.write("};\n\n")
        f.write("// Sprites: 16 colors x 4 pens (clut[color * 8 + pen] -> spr_pal)\n")
        f.write("const unsigned short mpatrol_spr_cmap[16][4] PROGMEM = {\n")
        transp = []
        for c in range(16):
            idx = [clut[c * 8 + p] & 0x1f for p in range(4)]
            transp.append(sum(1 << p for p in range(4) if idx[p] == 0))
            f.write("  { " + ", ".join(f"0x{c565(spr_rgb[i]):04x}" for i in idx) + " },\n")
        f.write("};\n\n")
        f.write("// Sprites: bit p set = pen p transparent (clut entry 0)\n")
        f.write("const unsigned char mpatrol_spr_transp[16] PROGMEM = { "
                + ", ".join(f"0x{t:02x}" for t in transp) + " };\n\n")
        f.write("// Backgrounds: mountains, hills, city x 4 pens\n")
        f.write("const unsigned short mpatrol_bg_cmap[3][4] PROGMEM = {\n")
        for pens in BG_PENS:
            f.write("  { " + ", ".join(f"0x{c565(bg_rgb[i]):04x}" for i in pens) + " },\n")
        f.write("};\n")
    print(f"  wrote {path}")


def emit(name, ctype, dims, count, flat, comment):
    path = os.path.join(OUT_DIR, f"{name}.h")
    with open(path, "w", newline="\n") as out:
        out.write("// Generated by romconv/internal/mpatrol/mpatrol_rom_convert.py; do not edit.\n")
        out.write(f"// {comment}\n")
        emit_compressed(out, name, ctype, dims, count, flat)
    print(f"  wrote {path}")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    rom = b"".join(load(e) for e in MAIN)
    snd = load(SOUND)
    tx = b"".join(load(e) for e in TX)
    sp = b"".join(load(e) for e in SP) + b"\x00" * 0x1000
    bgs = [decode_bg(load(e)) for e in BG]
    tx_pal, bg_pal, spr_pal, clut = load(TX_PAL), load(BG_PAL), load(SPR_PAL), load(SPR_CLUT)

    chars = decode_chars(tx)
    sprites = decode_sprites(sp)

    emit("mpatrol_rom", "unsigned char", "", len(rom), list(rom), "Z80 ROM 0x0000-0x3fff")
    emit("mpatrol_snd_rom", "unsigned char", "", len(snd), list(snd), "M6803 ROM, sound CPU 0x7000-0x7fff")
    emit("mpatrol_tilemap", "unsigned short", "[8]", 512, chars,
         "512 chars 8x8 2 bpp, row = 8 pens, pixel 0 in bits 15-14")
    emit("mpatrol_spritemap", "uint32_t", "[16]", 128, sprites,
         "128 sprites 16x16 2 bpp, row = 16 pens, pixel 0 in bits 31-30")
    emit("mpatrol_bg", "unsigned char", "[4096]", 3, [b for img in bgs for b in img],
         "mountains, hills, city: 256x64 2 bpp, 4 px per byte, pixel 0 in bits 7-6")

    tx_rgb, bg_rgb, spr_rgb = palettes(tx_pal, bg_pal, spr_pal)
    write_cmap(os.path.join(OUT_DIR, "mpatrol_cmap.h"), tx_rgb, bg_rgb, spr_rgb, clut)
    sizes = {"rom": len(rom), "snd": len(snd), "tilemap": len(chars) * 2,
             "spritemap": len(sprites) * 4, "bg": 3 * 4096}
    total = sum(sizes.values())
    print("  Arena budget: " + ", ".join(f"{k} {v}" for k, v in sizes.items())
          + f" = {total} / {ARENA_CAPACITY} bytes")
    assert total < ARENA_CAPACITY


if __name__ == "__main__":
    main()
