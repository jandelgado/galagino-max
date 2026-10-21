from collections.abc import Sequence

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.emit.asset import emit_compressed
from romconv.games.starforce.sets import SetSpec

_BPP = 3
_ROM_SIZE = 16384
_NUM_16 = 512
_NUM_32 = 128
_SIZE_16 = 16
_SIZE_32 = 32

_LAYOUT_X_16 = tuple(list(range(8)) + [8 * 8 + i for i in range(8)])
_LAYOUT_Y_16 = tuple([i * 8 for i in range(8)] + [(16 + i) * 8 for i in range(8)])
_LAYOUT_X_32 = tuple(list(range(8)) + [8 * 8 + i for i in range(8)] + [32 * 8 + i for i in range(8)] +
                     [40 * 8 + i for i in range(8)])
_LAYOUT_Y_32 = tuple([i * 8 for i in range(8)] + [(16 + i) * 8 for i in range(8)] +
                     [(64 + i) * 8 for i in range(8)] + [(80 + i) * 8 for i in range(8)])


def _rotate_90_cw(matrix: Sequence[Sequence[int]], width: int, height: int) -> list[list[int]]:
    new_matrix = [[0] * height for _ in range(width)]
    for y in range(height):
        for x in range(width):
            new_matrix[x][(height - 1) - y] = matrix[y][x]
    return new_matrix


def _decode_sprites(rom_data: Sequence[int], num_sprites: int, width: int, height: int,
                    layout_x: Sequence[int], layout_y: Sequence[int],
                    bytes_per_sprite_plane: int) -> list[list[list[int]]]:
    """MAME gfx_layout: plane 0 (first ROM of the region) is the MSB."""
    plane_size = len(rom_data) // _BPP
    plane_offsets = [i * plane_size for i in range(_BPP)]
    decoded_sprites = []

    for i in range(num_sprites):
        sprite_data = [[0] * width for _ in range(height)]
        char_base_offset = i * bytes_per_sprite_plane

        for y in range(height):
            for x in range(width):
                pixel_value = 0
                bit_offset = layout_y[y] + layout_x[x]

                for plane_idx in range(_BPP):
                    byte_addr = char_base_offset + (bit_offset // 8)
                    bit_pos = 7 - (bit_offset % 8)
                    rom_addr = plane_offsets[plane_idx] + byte_addr
                    pixel_bit = (rom_data[rom_addr] >> bit_pos) & 1
                    pixel_value |= pixel_bit << (_BPP - 1 - plane_idx)

                sprite_data[y][x] = pixel_value
        decoded_sprites.append(sprite_data)

    return decoded_sprites


def convert_sprites(ctx: Context, game: str, spec: SetSpec) -> None:
    full_rom_data = bytearray()
    for name in spec.sprites:
        data = ctx.rom(name)
        if len(data) != _ROM_SIZE:
            raise ConversionError(f"{name}: size {len(data)}, expected {_ROM_SIZE}")
        full_rom_data.extend(data)

    sprites_16 = _decode_sprites(full_rom_data, _NUM_16, _SIZE_16, _SIZE_16, _LAYOUT_X_16, _LAYOUT_Y_16, 32)
    sprites_16 = [_rotate_90_cw(s, _SIZE_16, _SIZE_16) for s in sprites_16]

    chunks_per_row = _SIZE_16 // 8
    flat = []
    for sprite in sprites_16:
        for row in sprite:
            for chunk_idx in range(chunks_per_row):
                packed_int = 0
                for x_in_chunk in range(8):
                    packed_int |= row[chunk_idx * 8 + x_in_chunk] << (_BPP * (7 - x_in_chunk))
                flat.append(packed_int)

    # Arena: compresses 4-8x, the main CPU ROM moved to flash in its place
    with ctx.header(f"{game}_sprites.h") as f:
        f.write("// File generato da starforce_sprite_conv.py (versione con logica di decodifica corretta)\n"
                "#include <stdint.h>\n\n")
        f.write(f"// Dati per {len(sprites_16)} sprite {_SIZE_16}x{_SIZE_16}, 3bpp, ruotati di 90 gradi CW.\n")
        emit_compressed(f, f"{game}_sprites_16x16", "uint32_t", "[%d][%d]" % (_SIZE_16, chunks_per_row),
                        len(sprites_16), flat)
        f.write("\n")

    # Not emitted: 32x32 sprite k is 16x16 sprites 4k+2 | 4k (top half) over
    # 4k+3 | 4k+1 (bottom half), after rotation. starforce::blit_sprite
    # composes it from the 16x16 set, saving 64K flash.
    sprites_32 = _decode_sprites(full_rom_data, _NUM_32, _SIZE_32, _SIZE_32, _LAYOUT_X_32, _LAYOUT_Y_32, 128)
    sprites_32 = [_rotate_90_cw(s, _SIZE_32, _SIZE_32) for s in sprites_32]
    for k, s32 in enumerate(sprites_32):
        for qy in range(2):
            for qx in range(2):
                s16 = sprites_16[4 * k + (0 if qx else 2) + qy]
                for y in range(16):
                    assert s32[qy * 16 + y][qx * 16:qx * 16 + 16] == s16[y], (k, qy, qx)
