from romconv.core.context import Context
from romconv.core.job import Job
from romconv.gfx import cmap
from romconv.gfx.rom import emit_rom
from romconv.gfx.sprites import spritemap
from romconv.gfx.tiles import charmap_frogger


def _convert(ctx: Context) -> None:
    emit_rom(ctx, "frogger_rom_cpu1", ["frogger.26", "frogger.27", "frsm3.7"], "frogger_rom1.h", compress=True)
    emit_rom(ctx, "frogger_rom_cpu2", ["frogger.608", "frogger.609", "frogger.610"], "frogger_rom2.h",
             compress=True)
    charmap_frogger(ctx, "frogger_tilemap", ["frogger.606", "frogger.607"], "frogger_tilemap.h", compress=True)
    spritemap(ctx, "frogger_sprites", "frogger", ["frogger.606", "frogger.607"], "frogger_spritemap.h",
              compress=True)
    cmap.dump_palette(ctx, "frogger_colormap", "frogger_cmap.h", cmap.palette(ctx, "pr-91.6l"))


JOB = Job(name="frogger", convert=_convert, machine="frogger", zips=("frogger.zip",))
