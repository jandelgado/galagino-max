from romconv.core.context import Context
from romconv.core.job import Job
from romconv.gfx import cmap
from romconv.gfx.rom import emit_rom
from romconv.gfx.sprites import spritemap
from romconv.gfx.tiles import charmap
from romconv.gfx.wavetable import wavetable


def _convert(ctx: Context) -> None:
    emit_rom(ctx, "digdug_rom_cpu1", ["dd1a.1", "dd1a.2", "dd1a.3", "dd1a.4"], "digdug_rom1.h", compress=True)
    emit_rom(ctx, "digdug_rom_cpu2", ["dd1a.5", "dd1a.6"], "digdug_rom2.h", compress=True)
    emit_rom(ctx, "digdug_rom_cpu3", ["dd1.7"], "digdug_rom3.h", compress=True)
    emit_rom(ctx, "digdug_playfield", ["dd1.10b"], "digdug_playfield.h", compress=True)

    charmap(ctx, "digdug_tilemap", "dd1.9", "digdug_tilemap.h", compress=True)
    charmap(ctx, "digdug_pftiles", "dd1.11", "digdug_pftiles.h", compress=True)

    spritemap(ctx, "digdug_sprites", "digdug", ["dd1.15", "dd1.14", "dd1.13", "dd1.12"], "digdug_spritemap.h",
              compress=True)

    palette = cmap.palette(ctx, "136007.113")
    cmap.colormap(ctx, "digdug_colormap_tiles", "136007.112", palette[0:16], "digdug_cmap_tiles.h")
    cmap.colormap(ctx, "digdug_colormap_sprites", "136007.111", palette[16:32], "digdug_cmap_sprites.h")
    cmap.dump_palette(ctx, "digdug_colormaps", "digdug_cmap.h", palette)

    wavetable(ctx, "digdug_wavetable", ["136007.110"], "digdug_wavetable.h")


JOB = Job(name="digdug", convert=_convert, machine="digdug", zips=("digdug.zip",))
