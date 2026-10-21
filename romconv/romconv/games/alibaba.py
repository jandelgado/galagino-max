# Ali Baba and 40 Thieves (Sega, 1982) -- ROM converter for galagino.
# Hardware: Pac-Man derived (same tile/sprite/color scheme, same Namco WSG
# sound). Verified against MAME src/mame/misc/pacman.cpp, ROM_START(alibaba),
# via exact CRC32 match of every ROM file (see project memory).
#
# ROMs read from romszip/alibaba.zip:
#   5e,5f,5h,5k      - gfx1 (tiles + sprites, 2KB each)
#   6e,6f,6h,6k,6l,6m - maincpu program ROM
#   ab7.bin          - gfx2 ("mystery" clock graphic)
#   82s123.e7        - master RGB palette PROM (32 bytes)
#   82s129.a4        - color lookup PROM (256 bytes)
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.gfx.color import prom_rgb_332, rgb565_swapped
from romconv.gfx.gfxutil import hex16, rot_galagino

_ALIBABA_FILES = (
    ("6e", "4e886a4a17f441f6d1d213c4c433b40dd38eefbc"),
    ("6f", "6b9a1fd11db9f521417566ae4c7065151aa272b5"),
    ("6h", "5381a4fcbc9fa97574c6df2978c7500164df75e5"),
    ("6k", "4516a60ec83e3c3388cd56f538f49afc86a50983"),
    ("6l", "6f3507ad10432f9123150b8bc1d0fb52372a412b"),
    ("6m", "7caaf668906b76d4947e988c444723b33f8e9726"),
    ("5e", "986170627953582b1e6fbca59e5c15cf8c4de9c7"),
    ("5h", "094d090bd0563f75d6ff1bfe2302cae941a89504"),
    ("5f", "ed6aee778295b0182d32846b5e41776b5b15420c"),
    ("5k", "a1609bae637207a82920678f05bcc10a5ff096de"),
    ("ab7.bin", "1d76e16c95cb2873d898a4151a902113fccafe1c"),
    ("82s123.e7", "8d0268dee78e47c712202b0ec4f1f51109b1f2a5"),
    ("82s129.a4", "19097b5f60d1030f8b82d9f1d3a241f93e5c75d6"),
    ("82s126.1m", "bbcec0570aeceb582ff8238a4bc8546a23430081"),
    ("82s126.3m", "0c4d0bee858b97632411c440bea6948a74759746"),
)


# ---------------------------------------------------------------------------
# Program ROM: CPU address space is non-contiguous
#   0x0000-0x3fff  6e,6f,6h,6k (4x 0x1000)
#   0x8000-0x8fff  6l          (1x 0x1000)
#   0xa000-0xa7ff  6m          (1x 0x0800), MAME: map(0xa000,0xa7ff).mirror(0x1800).rom()
#                  -> also readable/fetchable at 0xa800-0xafff, 0xb000-0xb7ff,
#                     0xb800-0xbfff (real HW: Z80 A15 wiring makes these alias
#                     the same 0x800-byte chip). MUST be replicated or any
#                     stray/legitimate fetch into those mirrors reads out of
#                     bounds of a short array (undefined behaviour / hangs).
# Build one FULL 0x10000-byte array (whole Z80 address space) with bytes
# placed at their real CPU offset (mirrors included); unused gaps are zero
# and should never be fetched in normal operation, but staying in-bounds
# for the full 64KB removes any risk of reading past the array.
# ---------------------------------------------------------------------------
def _convert_rom(ctx, roms):
    size = 0x10000
    rom = bytearray(size)

    def place(name, offset):
        data = roms[name]
        rom[offset:offset + len(data)] = data

    place("6e", 0x0000)
    place("6f", 0x1000)
    place("6h", 0x2000)
    place("6k", 0x3000)
    place("6l", 0x8000)

    bank6m = roms["6m"]  # 0x0800 bytes, mirrored 4x across 0xa000-0xbfff
    for base in (0xa000, 0xa800, 0xb000, 0xb800):
        rom[base:base + len(bank6m)] = bank6m

    with ctx.header("alibaba_rom.h") as f:
        f.write("// Ali Baba and 40 Thieves - main CPU ROM, full 64KB Z80 address space.\n")
        f.write("// Populated: 0x0000-0x3fff, 0x8000-0x8fff, 0xa000-0xbfff (0x800-byte bank\n")
        f.write("// 'ab7=6m' mirrored 4x per MAME map(0xa000,0xa7ff).mirror(0x1800).rom()).\n")
        f.write("// Rest is zero and never legitimately fetched, but kept in-bounds.\n")
        emit_compressed(f, "alibaba_rom", "unsigned char", "", size, list(rom))
    ctx.log.info(f"Written: alibaba_rom.h ( {size} bytes )")


# ---------------------------------------------------------------------------
# Generic MAME-style planar gfx decoder: given the raw combined region bytes
# and a gfx_layout description (as literal python lists matching the MAME
# C structs verbatim), decode `total` characters into a list of 2D pixel
# arrays (pen 0-3). This mirrors MAME's generic tile decoder exactly, so we
# do not need to special-case each layout by hand.
# ---------------------------------------------------------------------------
def _decode_layout(data, width, height, total, planes, planeoffset, xoffset, yoffset, charincrement):
    nbits = len(data) * 8
    chars = []
    for code in range(total):
        base_bit = code * charincrement
        img = [[0] * width for _ in range(height)]
        for y in range(height):
            for x in range(width):
                pen = 0
                for p in range(planes):
                    bitofs = base_bit + planeoffset[p] + yoffset[y] + xoffset[x]
                    if bitofs >= nbits:
                        bit = 0
                    else:
                        byte = data[bitofs // 8]
                        bit = (byte >> (7 - (bitofs % 8))) & 1
                    pen |= bit << p
                img[y][x] = pen
        chars.append(img)
    return chars


def _rgn_frac(num, den, region_len_bits):
    return (region_len_bits * num) // den


# The MAME xoffset/yoffset arrays decode each character in its NATIVE ROM
# orientation, which is landscape (MAME applies a separate whole-screen 90 deg
# rotation pass at display time because the physical monitor is mounted
# rotated -- see GAME(...,ROT90,...) for alibaba/pacman). This project renders
# straight into a portrait framebuffer with no such separate rotation pass, so
# every character decoded from ROM must be pre-rotated here instead. Verified
# empirically against a systematic set of candidates (plain, transposed, both
# bit orders, both rotation directions): 90 deg CLOCKWISE is the one that
# reproduces a correct, non-mirrored, correctly-sequenced font (digits 0-9,
# punctuation, then A-Z, all upright and in order).
_rotate_cw = rot_galagino


# same rotation, generalised to non-square images: an H x W image becomes a
# W x H image (used for the 24x16 "clock" graphic -> 16x24 after rotation).
def _rotate_cw_rect(img):
    h = len(img)
    w = len(img[0])
    return [[img[h - 1 - i][j] for i in range(h)] for j in range(w)]


# ---------------------------------------------------------------------------
# Tiles (8x8, 2bpp) + sprites (16x16, 2bpp) from "gfx1" = 5e+5h+5f+5k
# (0x800 bytes each, concatenated in THIS order to match MAME's ROM_LOAD).
# tilelayout:   total=RGN_FRAC(1,2), planes=2, planeoffset={0,4},
#               xoffset={8*8+0..3, 0..3}, yoffset={0,8,...,56}, charincrement=16*8
# spritelayout: total=RGN_FRAC(1,2), planes=2, planeoffset={0,4},
#               xoffset={8*8,8*8+1,8*8+2,8*8+3,16*8+0..3,24*8+0..3,0..3},
#               yoffset={0,8,...,56, 32*8,33*8,...,39*8}, charincrement=64*8
# (verified against E:\Download\pacman.cpp lines 3598-3621)
# ---------------------------------------------------------------------------
def _convert_tiles_sprites(ctx, roms):
    gfx1 = roms["5e"] + roms["5h"] + roms["5f"] + roms["5k"]
    region_bits = len(gfx1) * 8  # 0x2000 bytes = 0x10000 bits

    # --- tiles: decoded from offset 0x0000 of gfx1, 256 chars ---
    # NOTE: planeoffset swapped from the literal MAME {0,4} to {4,0} -- with
    # the literal order, 2-tone graphics (e.g. money bag, player sprite) came
    # out with foreground/background colours swapped (confirmed on real HW:
    # "should be white bag/blue $, shows blue bag/white $", same inversion on
    # the player sprite). A plane swap flips pen1<->pen2 while leaving pen0
    # (transparent) and pen3 unchanged, which exactly matches "two colours
    # swapped, single-colour graphics like most of the font unaffected".
    tile_total = _rgn_frac(1, 2, region_bits) // (16 * 8)
    tile_planeoffset = [4, 0]
    tile_xoffset = [8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 0, 1, 2, 3]
    tile_yoffset = [i * 8 for i in range(8)]
    tiles = _decode_layout(gfx1, 8, 8, tile_total, 2, tile_planeoffset,
                           tile_xoffset, tile_yoffset, 16 * 8)
    tiles = [_rotate_cw(t) for t in tiles]

    with ctx.header("alibaba_tilemap.h") as f:
        f.write("// Ali Baba tiles: 8x8, 2bpp, packed as one uint16 per row (2 bits/pixel)\n")
        flat = []
        for img in tiles:
            for y in range(8):
                word = 0
                for x in range(8):
                    word |= img[y][x] << (2 * x)
                flat.append(word)
        emit_compressed(f, "alibaba_tilemap", "unsigned short", "[8]", len(tiles), flat)
    ctx.log.info(f"Written: alibaba_tilemap.h ( {len(tiles)} tiles )")

    # --- sprites: decoded from offset 0x1000 of gfx1, 64 sprites, 16x16 ---
    # NOTE: RGN_FRAC is relative to the FULL "gfx1" region (region_bits, same
    # as used for tiles above), NOT to the byte-sliced sub-region below -- the
    # GFXDECODE_ENTRY offset (0x1000) only shifts where character 0 starts.
    sprite_region = gfx1[0x1000:]
    sprite_total = _rgn_frac(1, 2, region_bits) // (64 * 8)
    sprite_planeoffset = [4, 0]  # swapped, see note on tile_planeoffset above
    sprite_xoffset = [8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
                      16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
                      24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3,
                      0, 1, 2, 3]
    sprite_yoffset = [i * 8 for i in range(8)] + [32 * 8 + i * 8 for i in range(8)]
    sprites = _decode_layout(sprite_region, 16, 16, sprite_total, 2, sprite_planeoffset,
                             sprite_xoffset, sprite_yoffset, 64 * 8)

    # Stored unflipped; pacman::blit_sprite flips at draw time. Flags bit0
    # (hardware flipx) and bit1 (flipy) act in the ORIGINAL (pre-rotation)
    # orientation, which after rotate_cw are row order and pixel order of the
    # stored rows (spriteflip.h).
    with ctx.header("alibaba_spritemap.h") as f:
        f.write("// Ali Baba sprites: 16x16, 2bpp, packed as one uint32 per row, unflipped\n")
        flat = []
        for img in sprites:
            rimg = _rotate_cw(img)
            for y in range(16):
                word = 0
                for x in range(16):
                    word |= rimg[y][x] << (2 * x)
                flat.append(word)
        emit_compressed(f, "alibaba_sprites", "uint32_t", "[16]", len(sprites), flat)
    ctx.log.info(f"Written: alibaba_spritemap.h ( {len(sprites)} sprites )")


# ---------------------------------------------------------------------------
# "Mystery" clock graphic (gfx2 = ab7.bin, reloaded to fill 0x1000 bytes).
# alibaba_clocklayout: 24x16, 2bpp, total=RGN_FRAC(1,2), planeoffset =
#   {RGN_FRAC(1,2), RGN_FRAC(0,2)} i.e. plane0 from the SECOND half of the
#   (already reloaded) region, plane1 from the FIRST half -- since ROM_RELOAD
#   makes both halves byte-identical, both planes always carry the same bit,
#   so every pixel pen is 0 or 3 (2-tone graphic).
# xoffset spans 24 columns split 8/8/8 across three byte-groups (16*8,8*8,0),
# yoffset covers 16 rows in two groups of 8 (256+, 0+), charincrement=32*16.
# (verified against E:\Download\pacman.cpp lines 3624-3640)
# ---------------------------------------------------------------------------
def _convert_clock(ctx, roms):
    half = roms["ab7.bin"]  # 0x800, MAME reloads this to fill 0x800-0xfff too
    gfx2 = half + half
    region_bits = len(gfx2) * 8

    total = _rgn_frac(1, 2, region_bits) // (32 * 16)
    planeoffset = [_rgn_frac(1, 2, region_bits), _rgn_frac(0, 2, region_bits)]
    xoffset = [16 * 8 + 7, 16 * 8 + 6, 16 * 8 + 5, 16 * 8 + 4, 16 * 8 + 3, 16 * 8 + 2, 16 * 8 + 1, 16 * 8 + 0,
               8 * 8 + 7, 8 * 8 + 6, 8 * 8 + 5, 8 * 8 + 4, 8 * 8 + 3, 8 * 8 + 2, 8 * 8 + 1, 8 * 8 + 0,
               0 * 8 + 7, 0 * 8 + 6, 0 * 8 + 5, 0 * 8 + 4, 0 * 8 + 3, 0 * 8 + 2, 0 * 8 + 1, 0 * 8 + 0]
    yoffset = [256 + 0 * 8, 256 + 1 * 8, 256 + 2 * 8, 256 + 3 * 8, 256 + 4 * 8, 256 + 5 * 8, 256 + 6 * 8,
               256 + 7 * 8, 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8]

    clocks = _decode_layout(gfx2, 24, 16, total, 2, planeoffset, xoffset, yoffset, 32 * 16)
    # same physical-hardware rotation as tiles/sprites (see rotate_cw comment);
    # a 24-wide x 16-tall source becomes 16-wide x 24-tall after rotation.
    clocks = [_rotate_cw_rect(c) for c in clocks]

    with ctx.header("alibaba_clockmap.h") as f:
        f.write("// Ali Baba 'mystery' clock graphic: 16x24 (rotated from source 24x16,\n")
        f.write("// same hardware quirk as tiles/sprites), 2bpp (2-tone: plane0==plane1\n")
        f.write("// always, since the source ROM is byte-identical in both halves).\n")
        f.write("// Packed as one uint32 per row (16 pixels x 2 bits fits in 32 bits).\n")
        flat = []
        for img in clocks:
            for y in range(24):
                word = 0
                for x in range(16):
                    word |= img[y][x] << (2 * x)
                flat.append(word)
        emit_compressed(f, "alibaba_clockmap", "uint32_t", "[24]", len(clocks), flat)
    ctx.log.info(f"Written: alibaba_clockmap.h ( {len(clocks)} clock tiles )")


# ---------------------------------------------------------------------------
# Palette: standard Pac-Man-family weighted-resistor RGB decode.
#   82s123.e7 (32 bytes) = master RGB palette, 3 bits R / 3 bits G / 2 bits B
#   82s129.a4 (256 bytes) = color lookup: index = colorcode*4 + pen, low
#     nibble selects one of the 32 master colors.
# Same algorithm as MAME's pacman_state::palette() (standard for this
# hardware family, weights 0x21/0x47/0x97).
# ---------------------------------------------------------------------------
def _convert_colors(ctx, roms):
    master = roms["82s123.e7"]
    lookup = roms["82s129.a4"]

    # bit0 unused for blue (2 bits only), matches pacman convention
    # byte-swapped RGB565 for ESP32 SPI, same convention as other converters in this project
    palette = [rgb565_swapped(*prom_rgb_332(byte)) for byte in master]

    with ctx.header("alibaba_cmap.h") as f:
        f.write("// Ali Baba colormap: 64 color codes x 4 pens, RGB565 (byte-swapped)\n")
        f.write("const unsigned short alibaba_colormap[][4] = {\n")
        for code in range(64):
            pens = []
            for pen in range(4):
                idx = lookup[code * 4 + pen] & 0x1f
                pens.append(hex16(palette[idx]))
            f.write("{" + ",".join(pens) + "},\n")
        f.write("};\n")
    ctx.log.info("Written: alibaba_cmap.h (64 color codes)")


def _convert(ctx: Context) -> None:
    roms = {name: ctx.rom(name, sha1) for name, sha1 in _ALIBABA_FILES}
    _convert_rom(ctx, roms)
    _convert_tiles_sprites(ctx, roms)
    _convert_clock(ctx, roms)
    _convert_colors(ctx, roms)


JOB = Job(name="alibaba", convert=_convert, machine="alibaba", zips=("alibaba.zip",), deps=("pacman",))
