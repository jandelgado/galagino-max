from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.emit.asset import emit_compressed

_ROMS = ("h5-05.bin", "k5-06.bin")
_NUM_SPRITES = 128
_SIZE = 16
_BYTES_PER_SPRITE = 64
# pixel x offset within a row of the gfx_layout (STEP4(n*8+3, -1) for n = 0..3)
_X_OFFSETS = (*range(3, -1, -1), *range(11, 7, -1), *range(19, 15, -1), *range(27, 23, -1))


def convert_sprites(ctx: Context) -> None:
    rom_data = bytearray()
    for name in _ROMS:
        rom_data.extend(ctx.rom(name))
    if len(rom_data) != _NUM_SPRITES * _BYTES_PER_SPRITE:
        raise ConversionError(f"mrdo: sprite ROMs are {len(rom_data)} bytes, expected "
                              f"{_NUM_SPRITES * _BYTES_PER_SPRITE}")

    sprites = []
    for sprite_idx in range(_NUM_SPRITES):
        sprite_data = []
        base_sprite_bit_offset = sprite_idx * _BYTES_PER_SPRITE * 8

        for y in range(_SIZE):
            row_data = []
            row_bit_offset = base_sprite_bit_offset + (y * 32)

            for x in range(_SIZE):
                total_pixel_bit_offset = row_bit_offset + _X_OFFSETS[x]

                # bitplane 0 (LSB) at +0, bitplane 1 (MSB) at +4
                bit_offset_lsb = total_pixel_bit_offset
                lsb = (rom_data[bit_offset_lsb // 8] >> (7 - (bit_offset_lsb % 8))) & 1
                bit_offset_msb = total_pixel_bit_offset + 4
                msb = (rom_data[bit_offset_msb // 8] >> (7 - (bit_offset_msb % 8))) & 1

                row_data.append((msb << 1) | lsb)
            sprite_data.append(row_data)
        sprites.append(sprite_data)

    # rotated 90 degrees ccw, then flipped horizontally
    rotated = []
    for sprite in sprites:
        new_sprite = [[0] * _SIZE for _ in range(_SIZE)]
        for y in range(_SIZE):
            for x in range(_SIZE):
                new_sprite[(_SIZE - 1) - x][y] = sprite[y][x]
        rotated.append(new_sprite)
    sprites = [[row[::-1] for row in sprite] for sprite in rotated]

    with ctx.header("mrdo_sprites.h") as f:
        f.write(f"// File generato da script. Contiene i {_NUM_SPRITES} sprite (16x16, 2bpp) per Mr. Do! (Universal).\n")
        f.write("// Decodifica basata sulla definizione ufficiale del driver MAME.\n")
        f.write("// Gli sprite sono stati ruotati di 90 gradi in senso antiorario.\n")
        f.write("// Gli sprite sono stati flippati orizzontalmente.\n")
        f.write("#include <stdint.h>\n\n")
        flat = [v for sprite in sprites for row in sprite for v in row]
        emit_compressed(f, "mrdo_sprites", "unsigned char", "[%d][%d]" % (_SIZE, _SIZE), _NUM_SPRITES, flat)
