"""Convert the MAME scotrsht ROM set into galagino headers (MAME konami/scotrsht.cpp), ROT90.

Tiles and sprites use the circusc formats: tiles pre-rotated [8][4] (nibble LSB-first, low nibble =
even column), sprites landscape [16][8]. The palette bank is chosen at run time, so the 256 colors
and both lookups are emitted raw; the machine builds the per-bank color maps."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.write import write_array, write_asset, write_bytes
from romconv.gfx.color import rgb565_swapped
from romconv.gfx.gfxutil import mame_decode, rot_galagino, step
from romconv.gfx.rom import rom_bytes, rom_bytes_all
from romconv.plan.budget import log_sizes

# MAME ROM_START( scotrsht )
_G03 = ("gx545_g03_12c.bin", "d42b6979ade705a7522bd0bbc3eaa6d661580902")  # @8000, continue @4000
_G02 = ("gx545_g02_10c.bin", "07c21609c6cdfe2b8dd734d21086c5236ff8197b")  # @c000
_SOUND = ("gx545_g01_8c.bin", "73389fe04ce40da124d630dc3f8e58600d9556fc")
_TILES = ("gx545_g05_5f.bin", "ba45e6d18e56cc7fc49c8fda190ec152ce6bd15c")
_SPRITES = (
    ("gx545_g06_6f.bin", "6dfcf2abfa2ea056c948d82d35c55f033f3e4678"),
    ("gx545_h04_4f.bin", "6e89c738498d716fd43d9cc7b71b23438bd3c4b8"),
)
_PROMS = (
    ("gx545_6301_1f.bin", "0576cd0a738737c18143af887efd5ce76cdfc7cb"),  # red
    ("gx545_6301_2f.bin", "24937f2c9143e925c9becb488e11aa6daa807817"),  # green
    ("gx545_6301_3f.bin", "4ae6dfbb5c40a5ff97d7d80d0a441c1dc6dc5705"),  # blue
)
_TILE_LUT = ("gx545_6301_7f.bin", "a2ccf693bf378ce8dd311c4224ad20de59418f88")
_SPR_LUT = ("gx545_6301_8f.bin", "08452228bf13e43ce4a05806f79e9cd1542416f1")

_TILE_COUNT = 1024
_SPRITE_COUNT = 512
_HALF = 0x4000


def main_image(g03: bytes, g02: bytes) -> bytes:
    """CPU 0x4000-0xffff: g03 first half @8000, ROM_CONTINUE second half @4000, g02 @c000."""
    return g03[_HALF:] + g03[:_HALF] + g02


def palette(r: bytes, g: bytes, b: bytes) -> list[int]:
    """256 colors, pal4bit, rgb565 byte-swapped for the display."""
    return [rgb565_swapped((r[i] & 15) * 0x11, (g[i] & 15) * 0x11, (b[i] & 15) * 0x11) for i in range(256)]


def lut(prom: bytes) -> bytes:
    return bytes(v & 0x0f for v in prom)


def tiles(rom: bytes, count: int = _TILE_COUNT) -> list[int]:
    """gfx_8x8x4_packed_msb, rotated for the panel, [8][4] bytes, even column in the low nibble."""
    flat = []
    for t in mame_decode(rom, 8, 8, step(0, 1, 4), step(0, 4, 8), step(0, 32, 8), 256, count):
        r = rot_galagino(t)
        for row in r:
            flat.extend(row[2 * i] | (row[2 * i + 1] << 4) for i in range(4))
    return flat


def sprites(rom: bytes, count: int = _SPRITE_COUNT) -> list[int]:
    """16x16 (same layout as gberet), landscape, [16][8] bytes, even column in the low nibble."""
    xoffs = step(0, 4, 8) + step(256, 4, 8)
    yoffs = step(0, 32, 8) + step(512, 32, 8)
    flat = []
    for s in mame_decode(rom, 16, 16, step(0, 1, 4), xoffs, yoffs, 1024, count):
        for row in s:
            flat.extend(row[2 * i] | (row[2 * i + 1] << 4) for i in range(8))
    return flat


def _write_colors(ctx, pal, tile_lut, spr_lut):
    def emit_pal(f):
        f.write("// Tiles use 0x80 | bank << 4 | lut nibble, sprites bank << 4 | lut nibble.\n")
        f.write("const unsigned short scotrsht_palette[256] PROGMEM = {\n")
        for i in range(0, 256, 8):
            f.write("  " + ", ".join(f"0x{v:04x}" for v in pal[i:i + 8]) + ",\n")
        f.write("};\n")

    def emit_lut(f):
        both = [t | (s << 4) for t, s in zip(tile_lut, spr_lut)]
        f.write("// [color * 16 + pen]: low nibble 7f char lookup, high nibble 8f sprite lookup\n")
        f.write("const unsigned char scotrsht_lut[256] PROGMEM = {\n")
        for i in range(0, 256, 16):
            f.write("  " + ", ".join(f"0x{v:02x}" for v in both[i:i + 16]) + ",\n")
        f.write("};\n")
        f.write("// sprite pen is transparent when its nibble is 0\n")

    write_asset(ctx, "scotrsht_palette.h", "256 colors, pal4bit, rgb565 byte-swapped", emit_pal, __file__)
    write_asset(ctx, "scotrsht_lut.h", None, emit_lut, __file__)


def _convert(ctx: Context) -> None:
    main = main_image(rom_bytes(ctx, _G03), rom_bytes(ctx, _G02))
    sound = rom_bytes(ctx, _SOUND)
    tile_data = tiles(rom_bytes(ctx, _TILES))
    spr_data = sprites(rom_bytes_all(ctx, _SPRITES))
    pal = palette(*(rom_bytes(ctx, e) for e in _PROMS))

    write_bytes(ctx, "scotrsht_rom.h", "scotrsht_rom", main, "6809 ROM 0x4000-0xffff", __file__)
    write_bytes(ctx, "scotrsht_sound.h", "scotrsht_sound", sound, "Z80 sound ROM 0x0000-0x3fff", __file__)
    write_array(ctx, "scotrsht_tilemap.h", "scotrsht_tilemap", "unsigned char", "[8][4]", _TILE_COUNT, tile_data,
                "1024 tiles 8x8 4 bpp, rotated, nibble LSB-first", __file__)
    write_array(ctx, "scotrsht_spritemap.h", "scotrsht_spritemap", "unsigned char", "[16][8]", _SPRITE_COUNT,
                spr_data, "512 sprites 16x16 4 bpp, landscape, nibble LSB-first", __file__)
    _write_colors(ctx, pal, lut(rom_bytes(ctx, _TILE_LUT)), lut(rom_bytes(ctx, _SPR_LUT)))

    log_sizes(ctx, {"rom": len(main), "sound": len(sound), "tilemap": len(tile_data), "spritemap": len(spr_data)})


JOB = Job(name="scotrsht", convert=_convert, machine="scotrsht", zips=("scotrsht.zip",), plan=True)
