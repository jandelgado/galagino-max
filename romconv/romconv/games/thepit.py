"""The Pit ROM converter for galagino (MAME set thepit)."""
from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.gfx.bits import BIT
from romconv.gfx.color import prom_rgb_332, rgb565_swapped
from romconv.gfx.gfxutil import mame_decode, rot_galagino, step


_ARENA_CAPACITY = 182000


# MAME gfx1: pit8 at 0x0000, pit7 at 0x1000, planes { 0x1000*8, 0 }.
# Chars and sprites decode the same bytes.
def _gfx_region(pit7, pit8):
    region = bytearray(0x1800)
    region[0x0000:0x0800] = pit8
    region[0x1000:0x1800] = pit7
    return region


_PLANES = (0x1000 * 8, 0)


def _decode_chars(region):
    return mame_decode(region, 8, 8, _PLANES, step(0, 1, 8), step(0, 8, 8), 8 * 8, 256)


def _decode_sprites(region):
    xoffs = step(0, 1, 8) + step(8 * 8, 1, 8)
    yoffs = step(0, 8, 8) + step(16 * 8, 8, 8)
    return mame_decode(region, 16, 16, _PLANES, xoffs, yoffs, 32 * 8, 64)


# One word per galagino line, pixel 0 (leftmost column) in the low 2 bits.
def _pack_rows(tile):
    rows = []
    for row in rot_galagino(tile):
        v = 0
        for x, pen in enumerate(row):
            v |= pen << (2 * x)
        rows.append(v)
    return rows


# MAME thepit_palette: 32 PROM colors, then 8 primary colors used by the
# solid background layer (pens 32-39).
def _convert_palette(prom):
    pal = []
    for v in prom[:32]:
        pal.append(rgb565_swapped(*prom_rgb_332(v)))
    for i in range(8):
        pal.append(rgb565_swapped(0xFF * BIT(i, 2), 0xFF * BIT(i, 1), 0xFF * BIT(i, 0)))
    return pal


def _write(ctx, name, ctype, dims, count, flat, comment, elem_size):
    with ctx.header(name + ".h") as f:
        f.write(f"// {comment}\n")
        emit_compressed(f, name, ctype, dims, count, flat)
    size = len(flat) * elem_size
    ctx.log.info(f"Wrote: {name}.h ({size} bytes resident)")
    return size


def _convert(ctx: Context) -> None:
    main_rom = bytearray()
    for name, sha1 in [
        ("pit1.bin", "e64cb2f8d546f5d44dc10a4178f3d211882c45a9"),
        ("pit2.bin", "772ff81cf44d21981f9768f017af5cb81ff57be3"),
        ("pit3.bin", "8991c6424f008ddd15edac953635aecdba4ea696"),
        ("pit4.bin", "1dd9ded69121d28b674209abe1efd60afde6926d"),
        ("pit5.bin", "bb5133784b2b6a5c217d9ea42641daba2a797fff"),
    ]:
        main_rom += ctx.rom(name, sha1)
    sound_rom = ctx.rom("pit6.bin", "ba78b035a91a67732414ba327640fb771d4323c5")
    pit7 = ctx.rom("pit7.bin", "4a35dd857ca352e0260361376fe666af4b3315af")
    pit8 = ctx.rom("pit8.bin", "9baf094baab8325af659879cfb6984eeca0d94bd")
    prom = ctx.rom("82s123.ic4", "d188c90dba10fe3abaae92488786b555b35218c5")

    region = _gfx_region(pit7, pit8)
    tiles = [v for t in _decode_chars(region) for v in _pack_rows(t)]
    sprites = [v for s in _decode_sprites(region) for v in _pack_rows(s)]
    cmap = _convert_palette(prom)

    total = 0
    total += _write(ctx, "thepit_main_rom", "unsigned char", "", len(main_rom), list(main_rom),
                   "The Pit main CPU ROM", 1)
    total += _write(ctx, "thepit_sound_rom", "unsigned char", "", len(sound_rom), list(sound_rom),
                   "The Pit sound CPU ROM", 1)
    total += _write(ctx, "thepit_tiles", "unsigned short", "[8]", 256, tiles,
                   "The Pit chars: 256 x 8x8 2bpp, rotated, pixel 0 in bits 0-1", 2)
    total += _write(ctx, "thepit_sprites", "uint32_t", "[16]", 64, sprites,
                   "The Pit sprites: 64 x 16x16 2bpp, rotated, unflipped", 4)
    total += _write(ctx, "thepit_cmap", "unsigned short", "[4]", len(cmap) // 4, cmap,
                   "The Pit colors: rows 0-7 PROM palettes, rows 8-9 background 0-7", 2)

    ctx.log.info(f"Arena budget: {total} / {_ARENA_CAPACITY} bytes")
    if total >= _ARENA_CAPACITY:
        raise ConversionError("Arena budget exceeded")


JOB = Job(name="thepit", convert=_convert, machine="thepit", zips=("thepit.zip",))
