"""Phozon (Namco 1983). MAME namco/mappy.cpp (ROM_START phozon, gfx_phozon), mappy_v.cpp (phozon_palette)."""
from types import MappingProxyType

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.games.namco.common import CHAR_PLANES, CHAR_XOFFS, write_rom, write_wsg
from romconv.gfx.color import rgb565_swapped_scaled
from romconv.gfx.gfxutil import mame_decode, rot_galagino
from romconv.hw.namco import nudge

_ARENA_CAPACITY = 182000

_FILES = MappingProxyType({
    "main": (("6e.rom", "87a948b289356675d0418c87c3c0ae36ceba3ee0"),
             ("6h.rom", "b1d5146c009469d4c6695f08ea2c6ad5d05b5b9b"),
             ("6c.rom", "789881e94743efae01c63c1e3ce8d039cfa0324c"),
             ("6d.rom", "48585ac1eff8fb7ed35f56c767d725cae88ff128")),
    "sound": (("3b.rom", "2774681ea668403de31ea218d5df3ce64e3b9243"),),
    "sub": (("9r.rom", "2caef680229180b237f8c4becf052f1a96592efd"),),
    "chars": (("7j.rom", "12ef817136b45927d7f279952fa19049a1349f60"),
              ("8j.rom", "e3303656b4e8b988e55a9551e5344e289958f677")),
    "sprites": (("5t.rom", "4e9dda0d5ad1c1b8b3be7edb05b3060f5f63a9c7"),),
    "red": (("red.prm", "b24d9b3354d20a7ecc02c428245669c6c86bfd61"),),
    "green": (("green.prm", "b0ca7f8a77b7208cf974a8cc565fc91b7f40f51f"),),
    "blue": (("blue.prm", "e7b1ed698ab0e87872cb3a8f3ec102ca3a753259"),),
    "chrlut": (("chr.prm", "7b1899ca3f33f4561b572de1f24d9ea9d7d84b59"),),
    "sprlut": (("sprite.prm", "4305d37e613e1d15d37539b152c948648189c2cd"),),
    "wave": (("sound.prm", "072f427453efb1dda8147da61804fff06e1bc4d5"),),
})

_YOFFS_8 = tuple(y * 8 for y in range(8))

# spritelayout_8x8: 8x8x2bpp, 16 bytes per sprite
_SPR_PLANES = (0, 4)
_SPR_XOFFS = (0, 1, 2, 3, 64, 65, 66, 67)

_COUNT = 512
_TRANSPARENT_LUT = 0x0F  # lookup nibble 15: char pen 15 / sprite pen 31

# palette PROM bits 0..3 drive 2.2k, 1k, 470, 220 ohm (no pull resistor)
_RES = (2200, 1000, 470, 220)


def _load(ctx: Context, key: str) -> bytes:
    return b"".join(ctx.rom(name, sha1) for name, sha1 in _FILES[key])


def _color_level(nibble: int) -> int:
    w = [1.0 / r for r in _RES]
    return round(255 * sum(w[i] for i in range(4) if nibble & (1 << i)) / sum(w))


def _palette(red, green, blue) -> list[int]:
    return [nudge(rgb565_swapped_scaled(_color_level(red[i] & 0x0F),
                                        _color_level(green[i] & 0x0F),
                                        _color_level(blue[i] & 0x0F))) for i in range(32)]


def _flat_2bpp(tile) -> list[int]:
    # one unsigned short per galagino row, pixel 0 in bits 0-1
    out = []
    for row in tile:
        v = 0
        for x, p in enumerate(row):
            v |= p << (2 * x)
        out.append(v)
    return out


def _flipped(tile, flipx: int, flipy: int):
    rows = tile[::-1] if flipy else tile
    return [r[::-1] if flipx else list(r) for r in rows]


def _write_chars(ctx: Context, chars) -> int:
    flat = []
    for t in chars:
        flat += _flat_2bpp(rot_galagino(t))
    with ctx.header("phozon_tilemap.h") as f:
        print("// Phozon chars (7j+8j): 512 x 8x8 2bpp, rotated, pixel LSB-first", file=f)
        emit_compressed(f, "phozon_tilemap", "unsigned short", "[8]", len(chars), flat)
    return 2 * len(flat)


def _write_sprites(ctx: Context, sprites) -> int:
    # 4 variants per code, index code*4 + (flipy<<1 | flipx) in native MAME
    # flip terms; the flip is applied before the rotation
    flat = []
    for t in sprites:
        for v in range(4):
            flat += _flat_2bpp(rot_galagino(_flipped(t, v & 1, v >> 1)))
    with ctx.header("phozon_spritemap.h") as f:
        print("// Phozon sprites (5t): 512 x 8x8 2bpp x 4 flip variants, rotated", file=f)
        emit_compressed(f, "phozon_sprites", "unsigned short", "[8]", 4 * len(sprites), flat)
    return 2 * len(flat)


def _write_colormaps(ctx: Context, pal, chrlut, sprlut) -> None:
    def table(f, name, entries):
        print(f"const unsigned short {name}[][4] = {{", file=f)
        rows = ["{" + ",".join(hex(v) for v in entries[g * 4:g * 4 + 4]) + "}" for g in range(64)]
        print(",\n".join(rows), file=f)
        print("};", file=f)

    tiles = [pal[c & 0x0F] for c in chrlut]
    prio = [0 if (c & 0x0F) == _TRANSPARENT_LUT else pal[c & 0x0F] for c in chrlut]
    sprites = [0 if (c & 0x0F) == _TRANSPARENT_LUT else pal[16 + (c & 0x0F)] for c in sprlut]

    with ctx.header("phozon_cmap.h") as f:
        print("// Phozon colormaps: chars -> palette 0-15, sprites -> 16-31.", file=f)
        print("// 0x0000 = transparent (lookup 15): prio tiles and sprites only.", file=f)
        table(f, "phozon_colormap_tiles", tiles)
        table(f, "phozon_colormap_tiles_prio", prio)
        table(f, "phozon_colormap_sprites", sprites)


def _write_rom(ctx: Context, header: str, symbol: str, data: bytes, comment: str) -> int:
    write_rom(ctx, header, symbol, data, comment)
    return len(data)


def _convert(ctx: Context) -> None:
    chars = mame_decode(_load(ctx, "chars"), 8, 8, CHAR_PLANES, CHAR_XOFFS, _YOFFS_8, 128, _COUNT)
    sprites = mame_decode(_load(ctx, "sprites"), 8, 8, _SPR_PLANES, _SPR_XOFFS, _YOFFS_8, 128, _COUNT)

    sizes = {
        "phozon_rom_main": _write_rom(ctx, "phozon_rom_main.h", "phozon_rom_main", _load(ctx, "main"),
                                      "Phozon main M6809 ROM 0x8000-0xFFFF (6e+6h+6c+6d)"),
        "phozon_rom_sound": _write_rom(ctx, "phozon_rom_sound.h", "phozon_rom_sound", _load(ctx, "sound"),
                                       "Phozon sound M6809 ROM 0xE000-0xFFFF (3b)"),
        "phozon_rom_sub": _write_rom(ctx, "phozon_rom_sub.h", "phozon_rom_sub", _load(ctx, "sub"),
                                     "Phozon sub M6809 ROM 0xE000-0xFFFF (9r)"),
        "phozon_tilemap": _write_chars(ctx, chars),
        "phozon_sprites": _write_sprites(ctx, sprites),
    }

    pal = _palette(_load(ctx, "red"), _load(ctx, "green"), _load(ctx, "blue"))
    _write_colormaps(ctx, pal, _load(ctx, "chrlut"), _load(ctx, "sprlut"))
    write_wsg(ctx, "phozon_wavetable.h", "phozon_wavetable",
              "Phozon WSG 15XX waveforms (sound.prm): 8 x 32 samples, -8..7", _load(ctx, "wave"))

    for name, size in sizes.items():
        ctx.log.info(f"  {name:18s} {size:6d} B")
    total = sum(sizes.values())
    ctx.log.info(f"  Arena total        {total:6d} / {_ARENA_CAPACITY} B")
    if total >= _ARENA_CAPACITY:
        raise ConversionError("phozon: assets exceed Arena capacity")
    ctx.log.info("Phozon conversion finished.")


JOB = Job(name="phozon", convert=_convert, machine="phozon", zips=("phozon.zip",))
