from collections.abc import Sequence

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.emit.asset import emit_compressed

# MSB, bit 1, LSB planes (the decode order matters)
_ROMS = ("16_m07b.bin", "15_l07b.bin", "14_j07b.bin")
# (width, height, count, symbol)
_SETS = (
    (16, 16, 256, "bombjack_sprites_16x16"),
    (32, 32, 64, "bombjack_sprites_32x32"),
)


def _decode_sprite(rom_planes: Sequence[Sequence[int]], width: int, height: int,
                   base_offset: int) -> list[list[int]]:
    """One sprite, following floooh's GATHER macros."""
    sprite_matrix = [[0] * width for _ in range(height)]
    off = base_offset

    for y in range(height):
        if width == 16:
            bm0 = (rom_planes[2][off] << 8) | rom_planes[2][off + 8]
            bm1 = (rom_planes[1][off] << 8) | rom_planes[1][off + 8]
            bm2 = (rom_planes[0][off] << 8) | rom_planes[0][off + 8]
        elif width == 32:
            bm0 = (rom_planes[2][off] << 24) | (rom_planes[2][off + 8] << 16) | (rom_planes[2][off + 32] << 8) | rom_planes[2][off + 40]
            bm1 = (rom_planes[1][off] << 24) | (rom_planes[1][off + 8] << 16) | (rom_planes[1][off + 32] << 8) | rom_planes[1][off + 40]
            bm2 = (rom_planes[0][off] << 24) | (rom_planes[0][off + 8] << 16) | (rom_planes[0][off + 32] << 8) | rom_planes[0][off + 40]
        else:
            raise ConversionError(f"bombjack sprites: unsupported width {width}")

        for x in range(width):
            bit_pos = width - 1 - x
            sprite_matrix[y][x] = ((((bm2 >> bit_pos) & 1) << 0) |
                                   (((bm1 >> bit_pos) & 1) << 1) |
                                   (((bm0 >> bit_pos) & 1) << 2))

        off += 1
        if width == 16:
            if y == 7:
                off += 8
        elif width == 32:
            if (y & 7) == 7:
                off += 8
            if (y & 15) == 15:
                off += 32

    return sprite_matrix


def _sprite_values(data: Sequence[Sequence[int]]) -> list[int]:
    """Pixel rows as uint32_t, 8 pixels of 3 bits each."""
    vals = []
    width = len(data[0])
    for y_row in data:
        for chunk_start in range(0, width, 8):
            val = 0
            for pixel in y_row[chunk_start:chunk_start + 8]:
                val = (val << 3) | pixel
            vals.append(val)
    return vals


def convert_sprites(ctx: Context) -> None:
    # blitter rotates at run time: the data stays unrotated
    rom_data = [bytes(ctx.rom(name)) for name in reversed(_ROMS)]

    with ctx.header("bombjack_sprites.h") as f:
        f.write("// File generato automaticamente per gli sprite di Bomb Jack.\n")
        f.write("// Logica di decodifica basata sull'emulatore di floooh.\n")
        f.write("// Gli sprite NON sono stati ruotati. Il blitter deve gestire la rotazione.\n\n")

        for width, height, num_sprites, symbol in _SETS:
            sprite_byte_size = (width * height) // 8
            flat = []
            for i in range(num_sprites):
                flat.extend(_sprite_values(_decode_sprite(rom_data, width, height, i * sprite_byte_size)))
            emit_compressed(f, symbol, "uint32_t", "[%d]" % (width * height // 8), num_sprites, flat)
