# Pleiads (Tehkan 1981), MAME phoenix/phoenix.cpp, set pleiads: Phoenix
# board, ROT90. Tiles stored per native x column for the ROT90 line
# render: uint16_t[8], word[x] bits 2y = pixel at native y.
# Palette: 256 RGB565, pen = bank << 6 | fg << 5 | (code >> 5) << 2 | p,
# MAME pleiads_palette (bitswap 7,6,5,1,0,4,3,2), colors as MAME 0.289.
from types import MappingProxyType

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

# MAME 0.289 pleiads_palette() output per (low PROM, high PROM) nibble pair,
# read from the running driver (Lua palette dump, 2026-10-16). Its resnet
# (open collector, shared bias) couples the channels, e.g. pure red is
# ca1618, so a per-channel level table does not reproduce it. All 256 pens
# of this PROM set use these 15 pairs.
_MAME_COLORS = MappingProxyType({
    (0b0000, 0b0000): 0x000000, (0b0001, 0b0000): 0x9f1114, (0b0001, 0b0001): 0xca1618,
    (0b0010, 0b0010): 0x0a07bd, (0b0011, 0b0001): 0xd11eaa, (0b0011, 0b0011): 0xd31fd2,
    (0b0100, 0b0100): 0x2edf2d, (0b0101, 0b0100): 0xcdf040, (0b0101, 0b0101): 0xf6f644,
    (0b0110, 0b0000): 0x2ab5b6, (0b0110, 0b0010): 0x2db8e0, (0b0110, 0b0100): 0x35e7bf,
    (0b0110, 0b0110): 0x36e8e9, (0b0111, 0b0100): 0xd4f7d5, (0b0111, 0b0111): 0xffffff,
})


def _tiles(data):
    dec = mame_decode(data, 8, 8, [256 * 8 * 8, 0], [7, 6, 5, 4, 3, 2, 1, 0],
                      [8 * i for i in range(8)], 8 * 8, _TILE_COUNT)
    return column_words(dec, 2)


def _color(prom_lo, prom_hi, i):
    b = [(i >> n) & 1 for n in range(8)]
    col = b[7] << 7 | b[6] << 6 | b[5] << 5 | b[1] << 4 | b[0] << 3 | b[4] << 2 | b[3] << 1 | b[2]
    c = _MAME_COLORS[(prom_lo[col] & 0x0F, prom_hi[col] & 0x0F)]
    return rgb565_swapped(c >> 16, (c >> 8) & 0xFF, c & 0xFF)


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
    prom_lo = rom("7611-5.33", "252880d80425b2e697146e76efdc6cb9f3ba0378")
    prom_hi = rom("7611-5.26", "bdfab316ea26e2063879e7aa78b6ae2b55eb95c8")

    palette = [_color(prom_lo, prom_hi, i) for i in range(256)]

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
