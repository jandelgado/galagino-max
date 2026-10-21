"""Convert the MAME atetris (Tetris set 1) ROM set into galagino headers."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.write import write_array, write_bytes
from romconv.gfx.rom import rom_bytes
from romconv.plan.budget import check_budget

_MAIN = ("136066-1100.45f", "5e1189227f26563fd3e5372121ea5c915620f892")
_TILES = ("136066-1101.35a", "d8577985fc8ed4e74f74c68b7c00c4855b7c3270")


def _tile_words(gfx):
    # MAME gfx_8x8x4_packed_msb: 32 bytes per tile, 4 per row, high nibble
    # = left pixel, pen = nibble. Word per row: pixel i in bits 4i..4i+3.
    words = []
    for row in range(len(gfx) // 4):
        w = 0
        for i in range(8):
            b = gfx[row * 4 + (i >> 1)]
            w |= ((b >> 4) if i % 2 == 0 else (b & 15)) << (4 * i)
        words.append(w)
    return words


def _convert(ctx: Context) -> None:
    rom = rom_bytes(ctx, _MAIN)
    gfx = rom_bytes(ctx, _TILES)
    write_bytes(ctx, "atetris_rom.h", "atetris_rom", rom,
                "Main M6502 ROM, 64 KiB: 0x0000-0x7fff slapstic banks, 0x8000-0xffff fixed.", "atetris")
    words = _tile_words(gfx)
    write_array(ctx, "atetris_tiles.h", "atetris_tiles", "uint32_t", "[8]", len(words) // 8, words,
                "2048 8x8 tiles, uint32_t per row, pixel i in bits 4i..4i+3.", "atetris")
    check_budget(ctx, len(rom) + len(words) * 4)


JOB = Job(name="atetris", convert=_convert, machine="atetris", zips=("atetris.zip",))
