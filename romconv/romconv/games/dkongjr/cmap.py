from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.emit.asset import emit_compressed
from romconv.gfx.cmap import palette as prom_palette

_LOOKUP_ROM = "v-2n.bpr"
_PREFIX = "dkongjr_colormap"
_BANKS = 4


def convert_cmap(ctx: Context) -> None:
    # same rrrgggbb mapping as Donkey Kong, the low nibbles in c-2e, the high ones in c-2f
    palette = prom_palette(ctx, "c-2e.bpr", "c-2f.bpr")

    colormap_lookup_data = ctx.rom(_LOOKUP_ROM)
    if len(colormap_lookup_data) != 256:
        raise ConversionError(f"dkongjr: {_LOOKUP_ROM} is not 256 bytes")

    with ctx.header("dkongjr_cmap.h") as f:
        f.write("// File generato automaticamente per la colormap di Donkey Kong Jr.\n")
        f.write("// Palette da: dkongjrj.zip: c-2e.bpr, c-2f.bpr\n")
        f.write("// Lookup da:  dkongjrj.zip: v-2n.bpr\n\n")

        # tiles: 4 palette banks
        flat = []
        for s in range(_BANKS):
            for idx in colormap_lookup_data:
                offset_idx = idx + (16 * s)
                for p in range(4):
                    flat.append(palette[4 * offset_idx + p])
        emit_compressed(f, _PREFIX, "unsigned short", "[256][4]", _BANKS, flat)
        f.write("\n")

        # sprites use only 16 color indices per bank
        f.write(f"const unsigned short {_PREFIX}_sprite[][16][4] = {{\n")
        sprite_banks = []
        for s in range(_BANKS):
            sprite_entries = []
            for i in range(16):
                offset_idx = i + (16 * s)
                color_indices = [4 * offset_idx + k for k in range(4)]
                sprite_entries.append("{" + ",".join([hex(palette[ci]) for ci in color_indices]) + "}")
            sprite_banks.append("  {\n    " + ",\n    ".join(sprite_entries) + "\n  }")
        f.write(",\n".join(sprite_banks))
        f.write("\n};")
