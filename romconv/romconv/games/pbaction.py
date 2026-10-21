# ============================================================
# pbaction_rom_convert.py
#
# One-shot ROM converter for the Galagino Plus `pbaction` machine.
# The `pbaction` machine IS MAME `pbaction` = "Pinball Action (set 1)"
# (Tehkan, 1985, unencrypted Z80 + Z80 audio, ROT90).
#
# With no arguments it reads GalaginoPlus-main\romszip\pbaction.zip and writes
# the headers into GalaginoPlus-main\source\src\machines\pbaction\ (both paths
# resolved relative to this script, so the working directory does not matter).
# It verifies every size + CRC32 against the MAME `pbaction` set first, so a
# wrong/renamed dump is caught early.
#
# USAGE
#     python3 pbaction_rom_convert.py
#
# INPUT FILES  (MAME `pbaction` set 1 - `mame pbaction -verifyroms` must pass)
#     b-p7.bin b-n7.bin b-l7.bin           maincpu   0x0000 / 0x4000 / 0x8000
#     a-e3.bin                             audiocpu  0x0000 (0x2000)
#     a-s6.bin a-s7.bin a-s8.bin           fgchars   3 x 0x2000  (charlayout1, 3bpp)
#     a-j5.bin a-j6.bin a-j7.bin a-j8.bin  bgchars 4 x 0x4000 (charlayout2, 4bpp)
#     b-c7.bin b-d7.bin b-f7.bin           sprites   3 x 0x2000  (spritelayout1/2, 3bpp)
#
# OUTPUT HEADERS  (written to OUTDIR)
#     pbaction_main_rom.h    const unsigned char pbaction_main_rom[0xc000]
#     pbaction_audio_rom.h   const unsigned char pbaction_audio_rom[0x2000]
#     pbaction_fg_tiles.h    const unsigned char pbaction_fg_tiles[1024][8][8]   (pen 0-7)
#     pbaction_bg_tiles.h    const uint32_t pbaction_bg_tiles[2048][8]       (pen 0-15)
#     pbaction_sprites16.h   const uint32_t pbaction_sprites16[256][16][2]   (pen 0-7)
#
# bg tiles and sprites hold 8 pixels per uint32_t, pixel i in bits 4*i..4*i+3.
# The 32x32 sprites are not emitted: each is four 16x16 sprites (see
# check_sprites32), composed by pbaction::blit_sprite.
#
# All gfx are rotated 90 degrees clockwise here (ROT90 cabinet), matching the
# galagino blitter convention used by bnj / bombjack (rot_galagino()).
#
# NOT generated (hand-authored / not ROM-derived):
#     pbaction_dipswitches.h  - DIP config (verify vs `mame pbaction -listxml`)
#     pbaction_logo.h         - custom menu artwork (already present)
#     palette                 - pbaction has NO colour PROM; the palette is
#                               256 x xBGR_444 written by the CPU into palette
#                               RAM at 0xe400-0xe5ff and decoded live by the
#                               port. Nothing to extract.
#
# MAME reference: src/mame/tehkan/pbaction.cpp  (driver by Nicola Salmoria)
#   main_map            0x0000-0xbfff ROM
#   gfx_pbaction        GFXDECODE_START, charlayout1/2, spritelayout1/2
#   get_bg/fg_tile_info tile code math (bg: +0x10*(attr&0x70) -> 2048 codes;
#                                       fg: +0x10*(attr&0x30) -> 1024 codes)
# ============================================================
from types import MappingProxyType

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed, emit_plain
from romconv.gfx.gfxutil import mame_decode, rot_galagino



# --- MAME pbaction (set 1) ROM manifest: name -> (size, SHA1) -----------
_PBACTION_ROMS = MappingProxyType({
    # maincpu
    "b-p7.bin": "c9e605f9d291cb8c7163655ea96c605b7d30365f",
    "b-n7.bin": "a4c3205bfe5fba8bb1ff3ad15941a77c35b44a27",
    "b-l7.bin": "e75731d9bea80e0dc09798dd46e3b947fdb54aaa",
    # audiocpu
    "a-e3.bin": "df2827197cd55c3685e5ac8b26c20800623cb932",
    # fgchars (charlayout1, 3bpp)
    "a-s6.bin": "bd27439b91f41db3fd7eedb44e828d61b793bda0",
    "a-s7.bin": "7c8eff087f18cc2ff0572ea45e681a3a1ec94fad",
    "a-s8.bin": "74b6d926b8f456c8d0101f0232c5d3662423b396",
    # bgchars (charlayout2, 4bpp)
    "a-j5.bin": "0c0a05a26d793ba98b0f421d464ff4b1d301ff9e",
    "a-j6.bin": "18795ecbcd2da94f1cfcce5559d652388d1b8bc0",
    "a-j7.bin": "52ab15c63332f0fa98884fa9adc8d35b93c939c4",
    "a-j8.bin": "58f48d24903b797e8451bf231f9e8df621685d9f",
    # sprites (spritelayout1 16x16 + spritelayout2 32x32, 3bpp)
    "b-c7.bin": "69ad8e419e340d2f548468ed7838102789b978da",
    "b-d7.bin": "060f70ed6386c808303a488c97691257681bd8f3",
    "b-f7.bin": "56f47d25761b3850c49a3a81b5ea35f12bd77b14",
})

# --------------------------------------------------------------------------

# mame_decode / rot_galagino: see romconv/pyconv/gfxutil.py. planes[0] is
# the most-significant pen bit, planes[-1] the least, matching
# gfx_element::decode() in src/emu/drawgfx.cpp.


# --------------------------------------------------------------------------
def _rs_load(ctx: Context, name: str) -> bytearray:
    return ctx.rom(name, _PBACTION_ROMS[name])


def _build_main_rom(ctx: Context) -> bytes:
    """MAME ROM_REGION 0xc000 "maincpu":
        b-p7.bin @0x0000 (0x4000), b-n7.bin @0x4000 (0x4000),
        b-l7.bin @0x8000 (0x4000).  0xc000-0xffff = RAM / video / I/O."""
    cpu = bytearray(b"\xff" * 0xc000)
    cpu[0x0000:0x4000] = _rs_load(ctx, "b-p7.bin")
    cpu[0x4000:0x8000] = _rs_load(ctx, "b-n7.bin")
    cpu[0x8000:0xA000] = _rs_load(ctx, "b-l7.bin")
    return bytes(cpu)


def _build_audio_rom(ctx: Context) -> bytes:
    """MAME ROM_REGION 0x2000 "audiocpu": a-e3.bin @0x0000 (0x2000).
    The audio map only uses 0x0000-0x1fff as ROM, so 0x2000 bytes suffice."""
    return _rs_load(ctx, "a-e3.bin")


def _build_fg_tiles(ctx: Context) -> list[list[list[int]]]:
    """charlayout1 (fgchars), GFXDECODE gfx[0]:
        8,8  RGN_FRAC(1,3)  3 planes
        planeoffset { RGN_FRAC(0,3), RGN_FRAC(1,3), RGN_FRAC(2,3) }
        xoffs STEP8(0,1)   yoffs STEP8(0,8)   charincrement 8*8

    region = a-s6 + a-s7 + a-s8 (0x6000). planeoffset[0]=RGN_FRAC(0,3) is the
    MSB and points at a-s6; planeoffset[2]=RGN_FRAC(2,3) is the LSB -> a-s8.
    count = (0x6000*8) / 3 / (8*8) = 1024 tiles (matches the 0x400 fg codes
    the tile-info handler can address: videoram + 0x10*(attr&0x30))."""
    region = b"".join(_rs_load(ctx, n) for n in ("a-s6.bin", "a-s7.bin", "a-s8.bin"))
    rf = len(region) * 8 // 3               # RGN_FRAC(1,3) in bits
    planes = [0 * rf, 1 * rf, 2 * rf]       # MSB .. LSB
    xoffs = [i for i in range(8)]
    yoffs = [y * 8 for y in range(8)]
    bpt = 8 * 8
    count = rf // bpt
    assert count == 1024, count
    return mame_decode(region, 8, 8, planes, xoffs, yoffs, bpt, count)


def _build_bg_tiles(ctx: Context) -> list[list[list[int]]]:
    """charlayout2 (bgchars), GFXDECODE gfx[1]:
        8,8  RGN_FRAC(1,4)  4 planes
        planeoffset { RGN_FRAC(0,4), RGN_FRAC(1,4), RGN_FRAC(2,4), RGN_FRAC(3,4) }
        xoffs STEP8(0,1)   yoffs STEP8(0,8)   charincrement 8*8

    region = a-j5 + a-j6 + a-j7 + a-j8 (0x10000). planeoffset[0]=RGN_FRAC(0,4)
    is the MSB -> a-j5; planeoffset[3]=RGN_FRAC(3,4) is the LSB -> a-j8.
    count = (0x10000*8) / 4 / (8*8) = 2048 tiles (matches the 0x800 bg codes:
    videoram + 0x10*(attr&0x70))."""
    region = b"".join(_rs_load(ctx, n)
                      for n in ("a-j5.bin", "a-j6.bin", "a-j7.bin", "a-j8.bin"))
    rf = len(region) * 8 // 4
    planes = [0 * rf, 1 * rf, 2 * rf, 3 * rf]   # MSB .. LSB
    xoffs = [i for i in range(8)]
    yoffs = [y * 8 for y in range(8)]
    bpt = 8 * 8
    count = rf // bpt
    assert count == 2048, count
    return mame_decode(region, 8, 8, planes, xoffs, yoffs, bpt, count)


def _build_sprites16(ctx: Context) -> list[list[list[int]]]:
    """spritelayout1, GFXDECODE gfx[2] (offset 0x00000 into "sprites"):
        16,16  RGN_FRAC(1,3)  3 planes
        planeoffset { RGN_FRAC(0,3), RGN_FRAC(1,3), RGN_FRAC(2,3) }
        xoffs { STEP8(0,1), STEP8(64,1) }
        yoffs { STEP8(0,8), STEP8(128,8) }
        charincrement 32*8

    region = b-c7 + b-d7 + b-f7 (0x6000). planeoffset[0]=MSB -> b-c7,
    planeoffset[2]=LSB -> b-f7. count = (0x6000*8)/3/(32*8) = 256."""
    region = b"".join(_rs_load(ctx, n) for n in ("b-c7.bin", "b-d7.bin", "b-f7.bin"))
    rf = len(region) * 8 // 3
    planes = [0 * rf, 1 * rf, 2 * rf]
    xoffs = [i for i in range(8)] + [64 + i for i in range(8)]
    yoffs = [8 * i for i in range(8)] + [128 + 8 * i for i in range(8)]
    bpt = 32 * 8
    count = rf // bpt
    assert count == 256, count
    return mame_decode(region, 16, 16, planes, xoffs, yoffs, bpt, count)


def _build_sprites32(ctx: Context) -> list[list[list[int]]]:
    """spritelayout2, GFXDECODE gfx[3] (offset 0x01000 into "sprites"):
        32,32  RGN_FRAC(1,6)  3 planes
        planeoffset { RGN_FRAC(0,3), RGN_FRAC(1,3), RGN_FRAC(2,3) }
        xoffs { STEP8(0,1), STEP8(64,1), STEP8(256,1), STEP8(320,1) }
        yoffs { STEP8(0,8), STEP8(128,8), STEP8(512,8), STEP8(640,8) }
        charincrement 128*8

    NOTE the plane FRACTIONS here are thirds of the WHOLE 0x6000 region
    (RGN_FRAC(n,3)), while the tile count is RGN_FRAC(1,6) and the GFXDECODE
    entry starts 0x1000 BYTES into the region. So:
        region  = b-c7 + b-d7 + b-f7  (0x6000)
        rf3     = 0x6000*8/3          (plane stride, bits)
        base    = 0x1000*8            (start offset, bits)
        planes  = [0*rf3, 1*rf3, 2*rf3]   MSB..LSB  (b-c7 / b-d7 / b-f7)
        count   = RGN_FRAC(1,6) / (128*8) = (0x6000*8/6) / 1024 = 32
    """
    region = b"".join(_rs_load(ctx, n) for n in ("b-c7.bin", "b-d7.bin", "b-f7.bin"))
    rf3 = len(region) * 8 // 3
    base = 0x1000 * 8
    planes = [0 * rf3, 1 * rf3, 2 * rf3]
    xoffs = ([i for i in range(8)] + [64 + i for i in range(8)]
             + [256 + i for i in range(8)] + [320 + i for i in range(8)])
    yoffs = ([8 * i for i in range(8)] + [128 + 8 * i for i in range(8)]
             + [512 + 8 * i for i in range(8)] + [640 + 8 * i for i in range(8)])
    bpt = 128 * 8
    count = (len(region) * 8 // 6) // bpt
    assert count == 32, count
    return mame_decode(region, 32, 32, planes, xoffs, yoffs, bpt, count, base_bit=base)


# --- header writers ------------------------------------------------------
_BANNER = "// Generated from the MAME pbaction - Pinball Action (set 1) ROM set. Do not edit.\n"


def _write_bytes_header(ctx: Context, filename: str, guard: str, sym: str, data: bytes, note: str,
                       compress: bool = True) -> None:
    with ctx.header(filename) as f:
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write(_BANNER)
        f.write(f"// {note}\n\n")
        if compress:
            emit_compressed(f, sym, "unsigned char", "", len(data), list(data))
        else:
            body = ",\n".join("  " + ",".join("0x%02X" % b for b in data[i:i + 16])
                               for i in range(0, len(data), 16))
            emit_plain(f, sym, "unsigned char", "", len(data), body)
        f.write("\n#endif\n")
    ctx.log.info(f"  wrote {filename} {len(data):#8x} bytes")


def _write_tiles_header(ctx: Context, filename: str, guard: str, sym: str, tiles: list, dim: int, note: str,
                       compress: bool = True) -> None:
    with ctx.header(filename) as f:
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write(_BANNER)
        f.write(f"// {note}\n")
        f.write(f"// {len(tiles)} tiles, {dim}x{dim} pixels, rotated 90 deg CW (ROT90).\n\n")
        flat = [v for t in tiles for y in range(dim) for v in t[y]]
        if compress:
            emit_compressed(f, sym, "unsigned char", "[%d][%d]" % (dim, dim), len(tiles), flat)
        else:
            body_rows = []
            for i, t in enumerate(tiles):
                rows = ["{" + ",".join(str(v) for v in t[y]) + "}" for y in range(dim)]
                body_rows.append("  {" + ",".join(rows) + "}")
            emit_plain(f, sym, "unsigned char", "[%d][%d]" % (dim, dim), len(tiles), ",\n".join(body_rows))
        f.write("\n#endif\n")
    ctx.log.info(f"  wrote {filename} {len(tiles)} tiles ({dim}x{dim})")


def _write_packed_header(ctx: Context, filename: str, guard: str, sym: str, tiles: list, dim: int, note: str,
                        compress: bool) -> None:
    """tiles[i][y][x] (pen < 16) as uint32_t[dim][dim/8], 8 pixels per word."""
    words = dim // 8
    flat = [sum(t[y][w * 8 + i] << (4 * i) for i in range(8))
            for t in tiles for y in range(dim) for w in range(words)]
    inner = "[%d]" % dim if words == 1 else "[%d][%d]" % (dim, words)
    with ctx.header(filename) as f:
        f.write(f"#ifndef {guard}\n#define {guard}\n\n#include <stdint.h>\n\n")
        f.write(_BANNER)
        f.write(f"// {note}\n")
        f.write(f"// {len(tiles)} tiles, {dim}x{dim} pixels, rotated 90 deg CW (ROT90), "
                f"8 pixels per uint32_t, pixel i in bits 4*i.\n\n")
        if compress:
            emit_compressed(f, sym, "uint32_t", inner, len(tiles), flat)
        else:
            rows = [",".join("0x%08X" % v for v in flat[r * words:(r + 1) * words])
                    for r in range(len(tiles) * dim)]
            if words > 1:
                rows = ["{" + r + "}" for r in rows]
            body = ",\n".join("  {" + ",".join(rows[i * dim:(i + 1) * dim]) + "}"
                              for i in range(len(tiles)))
            emit_plain(f, sym, "uint32_t", inner, len(tiles), body)
        f.write("\n#endif\n")
    ctx.log.info(f"  wrote {filename} {len(tiles)} tiles ({dim}x{dim}, packed)")


def _check_sprites32(s16, s32) -> None:
    """32x32 sprite k (spritelayout2, +0x1000) is 16x16 sprites 128+4k+j,
    after rotation: j = 2 | 0 over 3 | 1 (first index half 0 | 1)."""
    for k, t in enumerate(s32):
        for qa in range(2):
            for qb in range(2):
                quad = s16[128 + 4 * k + (0 if qb else 2) + qa]
                for a in range(16):
                    assert t[qa * 16 + a][qb * 16:qb * 16 + 16] == quad[a], (k, qa, qb)


# --------------------------------------------------------------------------
def _convert(ctx: Context) -> None:
    _write_bytes_header(ctx, "pbaction_main_rom.h", "PBACTION_MAIN_ROM_H",
                       "pbaction_main_rom", _build_main_rom(ctx),
                       "Z80 main CPU space: b-p7 @0x0000, b-n7 @0x4000, b-l7 @0x8000; "
                       "0xa000+ is RAM/video/I/O (filled 0xFF). "
                       "Arena budget: PLAIN, compresses worst (~50%); bg tiles get the space.",
                       compress=False)
    _write_bytes_header(ctx, "pbaction_audio_rom.h", "PBACTION_AUDIO_ROM_H",
                       "pbaction_audio_rom", _build_audio_rom(ctx),
                       "Z80 audio CPU ROM a-e3.bin (0x0000-0x1fff).")

    fg = [rot_galagino(t) for t in _build_fg_tiles(ctx)]
    bg = [rot_galagino(t) for t in _build_bg_tiles(ctx)]
    s16 = [rot_galagino(t) for t in _build_sprites16(ctx)]
    s32 = [rot_galagino(t) for t in _build_sprites32(ctx)]

    _write_tiles_header(ctx, "pbaction_fg_tiles.h", "PBACTION_FG_TILES_H",
                       "pbaction_fg_tiles", fg, 8,
                       "Foreground chars (a-s6/s7/s8), charlayout1, 3bpp (pen 0-7). "
                       "gfx[0]: color = attr&0x0f, transparent pen 0.")
    _write_packed_header(ctx, "pbaction_bg_tiles.h", "PBACTION_BG_TILES_H",
                        "pbaction_bg_tiles", bg, 8,
                        "Background chars (a-j5/j6/j7/j8), charlayout2, 4bpp (pen 0-15). "
                        "gfx[1]: color = attr&0x07, opaque, palette base 128.",
                        compress=True)
    _write_packed_header(ctx, "pbaction_sprites16.h", "PBACTION_SPRITES16_H",
                        "pbaction_sprites16", s16, 16,
                        "Normal sprites (b-c7/d7/f7), spritelayout1, 3bpp (pen 0-7). "
                        "gfx[2]: color = spriteram[offs+1]&0x0f, transparent pen 0. "
                        "Also holds the 32x32 sprites (gfx[3]), see check_sprites32().",
                        compress=True)
    _check_sprites32(s16, s32)

    ctx.log.info("Pinball Action Conversion completed.")


JOB = Job(name="pbaction", convert=_convert, machine="pbaction", zips=("pbaction.zip",))
