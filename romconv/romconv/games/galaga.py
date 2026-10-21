from romconv.core.context import Context
from romconv.core.job import Job
from romconv.gfx import cmap
from romconv.gfx.rom import emit_rom
from romconv.gfx.sprites import spritemap
from romconv.gfx.tiles import charmap
from romconv.gfx.wavetable import wavetable


def _convert(ctx: Context) -> None:
    emit_rom(ctx, "galaga_rom_cpu1", ["gg1_1b.3p", "gg1_2b.3m", "gg1_3.2m", "gg1_4b.2l"], "galaga_rom1.h",
             apply_patches=True, compress=True)
    emit_rom(ctx, "galaga_rom_cpu2", ["gg1_5b.3f"], "galaga_rom2.h", compress=True)
    emit_rom(ctx, "galaga_rom_cpu3", ["gg1_7b.2c"], "galaga_rom3.h", compress=True)

    charmap(ctx, "galaga_tilemap", "gg1_9.4l", "galaga_tilemap.h", compress=True)
    spritemap(ctx, "galaga_sprites", "galaga", ["gg1_11.4d", "gg1_10.4f"], "galaga_spritemap.h", compress=True)

    palette = cmap.palette(ctx, "prom-5.5n")
    cmap.colormap(ctx, "galaga_colormap_sprites", "prom-3.1c", palette[0:16], "galaga_cmap_sprites.h")
    cmap.colormap(ctx, "galaga_colormap_tiles", "prom-4.2n", palette[16:32], "galaga_cmap_tiles.h")

    wavetable(ctx, "galaga_wavetable", ["prom-1.1d"], "galaga_wavetable.h")


# namco54_rom.h comes from the namco54 job
JOB = Job(name="galaga", convert=_convert, machine="galaga", zips=("galaga.zip",), deps=("namco54",))
