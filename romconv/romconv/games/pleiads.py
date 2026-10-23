# Pleiads (Tehkan 1981), MAME phoenix/phoenix.cpp, set pleiads: Phoenix
# board, ROT90. Tiles stored per native x column for the ROT90 line
# render: uint16_t[8], word[x] bits 2y = pixel at native y.
# Palette: 256 RGB565, pen = bank << 6 | fg << 5 | (code >> 5) << 2 | p,
# MAME pleiads_palette (bitswap 7,6,5,1,0,4,3,2).
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.emit.write import write_asset
from romconv.gfx.color import rgb565_swapped
from romconv.gfx.gfxutil import column_words, mame_decode
from romconv.plan.budget import log_sizes

_MAIN = (
    ("ic47.r1", "52a3232e99920805ce9e195b8a6338ae7044dd18"),
    ("ic48.r2", "312a33cca09d5d2d18992f28eb051230a90db6e3"),
    ("ic47.bin", "0f352b5461da957c564920fd1da83bc81f41ffb9"),
    ("ic48.bin", "6bfdc6c965199c5d4d687fe35dda057ec38cd8e0"),
    ("ic51.r5", "fd7937d0c114c8d9c1efaa9918ae3df2af41f032"),
    ("ic50.bin", "5c183e3a73fa882ffec3cb9219fb5619e625591a"),
    ("ic53.r7", "2ae687c84732942e69ad4dfb7a4ac1b97b77487a"),
    ("ic52.bin", "7e4ef298c8ddefc7dc0cbf94a9c9f36a4b807ba0"),
)
# The Z80 core runs this 8085 code; 8085 SIM (0x30, SOD output, not wired
# in MAME) would run as Z80 JR NC and jump into the sound latch writer at
# 27ad. The two SIMs in code (MAME disassembly; 27e3 executes every frame,
# 03f2 on the init path) become NOP. No 8085 RIM executes (host and MAME
# fetch traces, attract + gameplay).
_SIM, _NOP = 0x30, 0x00

_TILE_COUNT = 256

# MAME pleiads_palette(): compute_res_net_all(pleiads_net_info), then
# normalize_range(), which rescales luminance across all pens. The
# resnet (open collector, shared bias) couples the channels: pure red is
# ca1618, so a per-channel level table does not reproduce it.
_VCC, _VOL = 5.0, 0.05
_RBIAS, _RGND, _RIN = 100, 270, (270, 1)


def _tdiv(a, b):
    """C integer division, truncating toward zero."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def _res_net(inputs):
    """resnet.cpp compute_res_net(): high open collector inputs float."""
    g = 1 / _RBIAS + 1 / _RGND
    v = _VCC / _RBIAS
    for i, r in enumerate(_RIN):
        if not inputs >> i & 1:
            g += 1 / r
            v += _VOL / r
    return int(v / g * 255 / _VCC + 0.4)


def _normalize(rgb):
    """palette_t::normalize_range(0, 255) with its default 0..255 target."""
    ys = [299 * r + 587 * g + 114 * b for r, g, b in rgb]
    ymin, ymax = min(ys), max(ys)
    clamp = lambda x: min(255, max(0, x))
    out = []
    for (r, g, b), y in zip(rgb, ys):
        u = _tdiv((b - y // 1000) * 492, 1000)
        v = _tdiv((r - y // 1000) * 877, 1000)
        t = _tdiv((y - ymin) * 256, ymax - ymin)
        out.append((clamp(t + _tdiv(1140 * v, 1000)),
                    clamp(t - _tdiv(395 * u, 1000) - _tdiv(581 * v, 1000)),
                    clamp(t + _tdiv(2032 * u, 1000))))
    return out


def _palette(prom_26, prom_33):
    """256 RGB565 pens; 5.26 holds bit 0, 5.33 bit 1 of each channel."""
    rgb = _normalize([(_res_net(lo & 1 | hi << 1 & 2), _res_net(lo >> 2 & 1 | hi >> 1 & 2),
                       _res_net(lo >> 1 & 1 | hi & 2)) for lo, hi in zip(prom_26, prom_33)])
    pens = []
    for i in range(256):
        b = [(i >> n) & 1 for n in range(8)]
        col = b[7] << 7 | b[6] << 6 | b[5] << 5 | b[1] << 4 | b[0] << 3 | b[4] << 2 | b[3] << 1 | b[2]
        pens.append(rgb565_swapped(*rgb[col]))
    return pens


def _tiles(data):
    dec = mame_decode(data, 8, 8, [256 * 8 * 8, 0], [7, 6, 5, 4, 3, 2, 1, 0],
                      [8 * i for i in range(8)], 8 * 8, _TILE_COUNT)
    return column_words(dec, 2)


def _convert(ctx: Context) -> None:
    def rom(name, sha1):
        return ctx.rom(name, sha1)

    main = bytearray(b"".join(rom(n, s) for n, s in _MAIN))
    for addr in (0x03F2, 0x27E3):
        assert main[addr] == _SIM, f"no SIM at {addr:04x}"
        main[addr] = _NOP
    main = bytes(main)

    bg = (rom("ic23.bin", "da023a94725dc40107cd97e4decfd4dc0f9f00ee")
          + rom("ic24.bin", "421dedc674c6dde7abf01412df035a8eb8e6db9b"))
    fg = (rom("ic39.bin", "cd240bd056f761b2f9e2142049434f02cae3e315")
          + rom("ic40.bin", "8349008ab1d8ef08775b54170c37deb1d391fffc"))
    palette = _palette(rom("7611-5.26", "bdfab316ea26e2063879e7aa78b6ae2b55eb95c8"),
                       rom("7611-5.33", "252880d80425b2e697146e76efdc6cb9f3ba0378"))

    def write(name, fn):
        write_asset(ctx, name, None, fn)

    write("pleiads_rom.h", lambda f: emit_compressed(f, "pleiads_rom", "unsigned char", "", len(main), main))

    def tiles_header(f):
        emit_compressed(f, "pleiads_bgtiles", "unsigned short", "[8]", _TILE_COUNT, _tiles(bg))
        emit_compressed(f, "pleiads_fgtiles", "unsigned short", "[8]", _TILE_COUNT, _tiles(fg))

    write("pleiads_tiles.h", tiles_header)
    write("pleiads_palette.h",
          lambda f: emit_compressed(f, "pleiads_palette", "unsigned short", "", 256, palette))

    sizes = {"main": len(main), "bg": _TILE_COUNT * 16, "fg": _TILE_COUNT * 16, "palette": 512}
    log_sizes(ctx, sizes)


JOB = Job(name="pleiads", convert=_convert, machine="pleiads", zips=("pleiads.zip",))
