from collections.abc import Sequence

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.emit.asset import emit_compressed, emit_plain
from romconv.emit.write import module_path
from romconv.games.starforce.sets import SetSpec

_BPP = 3
_BG_SIZE = 16
_BG1_TILES = 256
_BG2_TILES = 256
_BG3_TILES = 128  # the smallest layer: PLAIN, it leaves the Arena to the main CPU ROM
_FG_SIZE = 8
_FG_TILES = 512
_FG_ROM_SIZE = 4096

# MAME gfx_layout bit offsets of a 16x16 tile
_BG_LAYOUT_X = (0, 1, 2, 3, 4, 5, 6, 7, 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 8 * 8 + 4, 8 * 8 + 5,
                8 * 8 + 6, 8 * 8 + 7)
_BG_LAYOUT_Y = (0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8, 16 * 8, 17 * 8, 18 * 8, 19 * 8, 20 * 8,
                21 * 8, 22 * 8, 23 * 8)


def _decode_bg_tiles(roms: Sequence[Sequence[int]], num_tiles: int) -> list[list[list[int]]]:
    """16x16 3bpp tiles from three plane ROMs, the first ROM being the MSB plane."""
    plane0_data, plane1_data, plane2_data = roms
    bytes_per_tile_plane = (_BG_SIZE * _BG_SIZE) // 8
    expected_size = num_tiles * bytes_per_tile_plane
    if any(len(plane) != expected_size for plane in roms):
        raise ConversionError(f"starforce bg tiles: ROM size is not {expected_size} bytes for {num_tiles} tiles")

    decoded_tiles = []
    for tile_idx in range(num_tiles):
        tile_data = [[0] * _BG_SIZE for _ in range(_BG_SIZE)]
        char_base_offset = tile_idx * bytes_per_tile_plane

        for y in range(_BG_SIZE):
            for x in range(_BG_SIZE):
                bit_offset = _BG_LAYOUT_Y[y] + _BG_LAYOUT_X[x]
                byte_addr = char_base_offset + (bit_offset // 8)
                bit_pos = 7 - (bit_offset % 8)

                bit_msb = (plane0_data[byte_addr] >> bit_pos) & 1
                bit_mid = (plane1_data[byte_addr] >> bit_pos) & 1
                bit_lsb = (plane2_data[byte_addr] >> bit_pos) & 1
                tile_data[y][x] = (bit_msb << 2) | (bit_mid << 1) | bit_lsb

        decoded_tiles.append(tile_data)

    return decoded_tiles


def _write_bg_tiles(ctx: Context, game: str, layer: str, tiles: Sequence[Sequence[Sequence[int]]],
                    plain: bool) -> None:
    num_tiles = len(tiles)
    chunks_per_row = _BG_SIZE // 8
    with ctx.header(f"{game}_{layer}_tiles.h") as f:
        f.write(f"// File generato da {module_path(__file__)}\n")
        f.write(f"// Contiene i {num_tiles} tiles di background ({_BG_SIZE}x{_BG_SIZE}, 3bpp).\n")
        f.write("// I tiles non sono stati ruotati.\n")
        f.write("// L'ordine dei bitplane è stato INVERTITO.\n")
        f.write("#include <stdint.h>\n\n")

        # a 16 pixel row as two uint32_t of 8 pixels
        flat = []
        for tile in tiles:
            for row in tile:
                for chunk_idx in range(chunks_per_row):
                    packed_int = 0
                    for x_in_chunk in range(8):
                        packed_int |= row[chunk_idx * 8 + x_in_chunk] << (_BPP * (7 - x_in_chunk))
                    flat.append(packed_int)

        inner_dims = "[%d][%d]" % (_BG_SIZE, chunks_per_row)
        symbol = f"{game}_{layer}_tilemap"
        if not plain:
            emit_compressed(f, symbol, "uint32_t", inner_dims, num_tiles, flat)
            return

        rows = ["{" + ",".join("0x%08X" % v for v in flat[i:i + chunks_per_row]) + "}"
                for i in range(0, len(flat), chunks_per_row)]
        body = ",\n".join("  {" + ",".join(rows[t * _BG_SIZE:(t + 1) * _BG_SIZE]) + "}" for t in range(num_tiles))
        emit_plain(f, symbol, "uint32_t", inner_dims, num_tiles, body)


def convert_bg_tiles(ctx: Context, game: str, spec: SetSpec) -> None:
    layers = (
        ("bg1", spec.bg1, _BG1_TILES, False),
        ("bg2", spec.bg2, _BG2_TILES, False),
        ("bg3", spec.bg3, _BG3_TILES, True),
    )
    for layer, names, num_tiles, plain in layers:
        tiles = _decode_bg_tiles([ctx.rom(name) for name in names], num_tiles)
        _write_bg_tiles(ctx, game, layer, tiles, plain)


def _rotate_90_cw(tile: Sequence[Sequence[int]]) -> list[list[int]]:
    new_tile = [[0] * _FG_SIZE for _ in range(_FG_SIZE)]
    for y in range(_FG_SIZE):
        for x in range(_FG_SIZE):
            new_tile[x][(_FG_SIZE - 1) - y] = tile[y][x]
    return new_tile


def convert_fg_tiles(ctx: Context, game: str, spec: SetSpec) -> None:
    plane0_data, plane1_data, plane2_data = (ctx.rom(name) for name in spec.fg)
    if any(len(plane) != _FG_ROM_SIZE for plane in (plane0_data, plane1_data, plane2_data)):
        raise ConversionError(f"{game} fg tiles: ROM size is not {_FG_ROM_SIZE} bytes")

    # the first ROM is the MSB plane
    tiles = []
    for tile_idx in range(_FG_TILES):
        tile_data = []
        base_offset = tile_idx * _FG_SIZE
        for y in range(_FG_SIZE):
            byte_p0 = plane0_data[base_offset + y]
            byte_p1 = plane1_data[base_offset + y]
            byte_p2 = plane2_data[base_offset + y]

            row_data = []
            for x in range(_FG_SIZE):
                bit_shift = 7 - x
                row_data.append((((byte_p0 >> bit_shift) & 1) << 2) | (((byte_p1 >> bit_shift) & 1) << 1) |
                                ((byte_p2 >> bit_shift) & 1))
            tile_data.append(row_data)
        tiles.append(tile_data)

    tiles = [_rotate_90_cw(tile) for tile in tiles]

    with ctx.header(f"{game}_fg_tiles.h") as f:
        f.write(f"// File generato da {module_path(__file__)}\n")
        f.write(f"// Contiene i {_FG_TILES} tiles di foreground (8x8, 3bpp) per Star Force.\n")
        f.write("// Ogni riga di 8 pixel (ciascuno di 3 bit) è impacchettata in un intero a 32 bit.\n")
        f.write("// I tiles sono stati ruotati di 90 gradi in senso orario.\n")
        f.write("#include <stdint.h>\n\n")

        flat = []
        for tile in tiles:
            for row in tile:
                packed_row_int = 0
                for x, pixel_value in enumerate(row):
                    packed_row_int |= pixel_value << (_BPP * (_FG_SIZE - 1 - x))
                flat.append(packed_row_int)
        emit_compressed(f, f"{game}_fg_tilemap", "uint32_t", "[%d]" % _FG_SIZE, _FG_TILES, flat)
