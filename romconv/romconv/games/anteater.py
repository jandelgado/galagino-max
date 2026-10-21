from romconv.core.context import Context
from romconv.core.job import Job
from romconv.gfx import cmap
from romconv.gfx.rom import emit_rom
from romconv.gfx.sprites import spritemap
from romconv.gfx.tiles import charmap_anteater


def _convert(ctx: Context) -> None:
    emit_rom(ctx, "anteater_rom_cpu1", ["ra1-2c", "ra1-2e", "ra1-2f", "ra1-2h"], "anteater_rom1.h", compress=True)
    emit_rom(ctx, "anteater_rom_cpu2", ["ra4-5c", "ra4-5d"], "anteater_rom2.h", compress=True)
    charmap_anteater(ctx, "anteater_tilemap", ["ra6-5f", "ra6-5h"], "anteater_tilemap.h", compress=True)
    spritemap(ctx, "anteater_sprites", "anteater", ["ra6-5f", "ra6-5h"], "anteater_spritemap.h", compress=True)
    cmap.dump_palette(ctx, "anteater_colormap", "anteater_cmap.h", cmap.palette(ctx, "colr6f.cpu"))


JOB = Job(name="anteater", convert=_convert, machine="anteater", zips=("anteater.zip",), deps=("frogger",))
