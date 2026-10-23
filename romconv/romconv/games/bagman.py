from romconv.core.context import Context
from romconv.core.job import Job
from romconv.gfx import cmap
from romconv.gfx.rom import emit_rom
from romconv.gfx.sprites import spritemap
from romconv.gfx.tiles import charmap_bagman


def _convert(ctx: Context) -> None:
    emit_rom(ctx, "bagman_rom_cpu", ["bagmanm2.1", "bagmanm2.2", "bagmanm2.3"], "bagman_rom.h", compress=True)
    charmap_bagman(ctx, "bagman_tilemap", ["bagmanm2.9", "bagmanm2.7"], "bagman_tilemap.h", compress=True)
    spritemap(ctx, "bagman_sprites", "bagman", ["bagmanm2.9", "bagmanm2.7"], "bagman_spritemap.h", compress=True)
    cmap.dump_palette(ctx, "bagman_colormap", "bagman_cmap.h", cmap.palette(ctx, "bagmanmc.clr"))


JOB = Job(name="bagman", convert=_convert, machine="bagman", zips=("bagmanm2.zip",))
