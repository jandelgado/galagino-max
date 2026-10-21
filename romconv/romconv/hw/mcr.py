"""Midway MCR graphics and SSIO PROM decoding shared by tron and dotron (MAME mcr_m.cpp, midway.cpp).

Decoders return pen grids [y][x] in native orientation; each game rotates,
mirrors and packs them for its panel layout.
"""
from romconv.gfx.gfxutil import mame_decode, step


def decode_tiles_raw(rom):
    # mcr_bg_layout: 8x8, planes {half+0, half+1, 0, 1}, 16 bytes per tile
    half = len(rom) * 4
    count = half // 128
    return mame_decode(rom, 8, 8, [half, half + 1, 0, 1], step(0, 2, 8), step(0, 16, 8), 128, count)


def decode_sprites_raw(rom):
    # mcr_sprite_layout: 32x32, planes STEP4(0,1), columns spread over the 4 ROM quarters
    q = len(rom) * 2  # RGN_FRAC(1,4) in bits
    count = q // 1024
    xoffs = []
    for byte in (0, 8, 16, 24):
        for quarter in range(4):
            xoffs += [quarter * q + byte, quarter * q + byte + 4]
    return mame_decode(rom, 32, 32, step(0, 1, 4), xoffs, step(0, 32, 32), 1024, count)


def duty_gain(prom):
    # MAME midway_ssio_device::compute_ay8910_modulation, volume in percent,
    # here as gain / 16 per duty nibble
    lookup = [0] * 16
    for volval in range(16):
        remaining = volval
        cur, prev = 0, 1
        clock = 0
        while clock < 160 and remaining:
            cur = prom[clock // 8] & (0x80 >> (clock % 8))
            if cur == 0 and prev != 0:
                remaining -= 1
            prev = cur
            clock += 1
        lookup[15 - volval] = clock * 100 // 160
    return [round(16 * v / 100) for v in lookup]
