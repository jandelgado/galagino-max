from collections.abc import Sequence

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.gfx.color import rgb565_swapped

_PALETTE_RG_ROM = "dkc1-c.1d"
_PALETTE_B_ROM = "dkc1-c.1c"
_LOOKUP_ROM = "dkc1-v.2n"
_PALETTE_SIZE = 512
_TILE_COLORS = 256
_PREFIX = "dkong3_colormap"


def _master_palette(prom_rg: Sequence[int], prom_b: Sequence[int]) -> list[int]:
    """512 colors: tiles (0-255) from the inverted PROM data, sprites (256-511) not inverted (MAME)."""
    if len(prom_rg) < _PALETTE_SIZE or len(prom_b) < _PALETTE_SIZE:
        raise ConversionError(f"dkong3: {_PALETTE_RG_ROM} and {_PALETTE_B_ROM} must be {_PALETTE_SIZE} bytes")

    master_palette = []
    for i in range(_PALETTE_SIZE):
        if i < _TILE_COLORS:
            byte_rg = ~prom_rg[i] & 0xFF
            byte_b = ~prom_b[i] & 0xFF
        else:
            byte_rg = prom_rg[i]
            byte_b = prom_b[i]

        r4 = (byte_rg >> 4) & 0x0F
        g4 = byte_rg & 0x0F
        b4 = byte_b & 0x0F

        r8 = int((r4 / 15.0) * 255)
        g8 = int((g4 / 15.0) * 255)
        b8 = int((b4 / 15.0) * 255)

        master_palette.append(rgb565_swapped(r8, g8, b8))

    return master_palette


def convert_cmap(ctx: Context) -> None:
    master_palette = _master_palette(ctx.rom(_PALETTE_RG_ROM), ctx.rom(_PALETTE_B_ROM))
    tile_palette = master_palette[:_TILE_COLORS]
    sprite_palette = master_palette[_TILE_COLORS:]
    colormap_lookup_data = ctx.rom(_LOOKUP_ROM)

    with ctx.header("dkong3_cmap.h") as f:
        f.write("// File generato automaticamente per la colormap di Donkey Kong 3.\n\n")

        # tile palette
        f.write(f"const unsigned short {_PREFIX}_tile_palette[256] = {{\n  ")
        color_hex = [hex(c) for c in tile_palette]
        for i in range(len(color_hex)):
            f.write(color_hex[i])
            if i < len(color_hex) - 1:
                f.write(", ")
            if (i + 1) % 16 == 0:
                f.write("\n  ")
        f.write("\n};\n\n")

        # tile lookup table
        f.write(f"const unsigned char {_PREFIX}_tile_lookup[256] = {{\n  ")
        f.write(", ".join([hex(val & 0x0F) for val in colormap_lookup_data]))
        f.write("\n};\n\n")

        # precomputed sprite colormap
        f.write(f"const unsigned short {_PREFIX}_sprite[64][4] = {{\n")
        sprite_entries = []
        for i in range(64):
            base_idx = i * 4
            color_group = [sprite_palette[base_idx + k] for k in range(4)]
            sprite_entries.append("  /* Palette " + str(i) + " */ {" + ",".join([hex(c) for c in color_group]) + "}")
        f.write(",\n".join(sprite_entries))
        f.write("\n};")


def convert_color_codes(ctx: Context) -> None:
    rom_data = ctx.rom(_LOOKUP_ROM)
    if len(rom_data) != 256:
        raise ConversionError(f"dkong3: {_LOOKUP_ROM} is not 256 bytes")

    with ctx.header("dkong3_color_codes.h") as f:
        f.write("// File generato automaticamente per la PROM dei codici colore dei tile di Donkey Kong 3.\n")
        f.write("// Dati estratti da: dkong3.zip: dkc1-v.2n\n\n")
        f.write("const unsigned char color_codes_prom[256] = {\n")

        lines = []
        for i in range(0, 256, 16):
            hex_values = [hex(val) for val in rom_data[i:i + 16]]
            lines.append("  " + ", ".join(hex_values))
        f.write(",\n".join(lines))
        f.write("\n};")
