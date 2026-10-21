from romconv.core.context import Context
from romconv.core.job import Job
from romconv.gfx import cmap
from romconv.gfx.rom import emit_rom
from romconv.gfx.sprites import spritemap
from romconv.gfx.tiles import charmap


def _convert(ctx: Context) -> None:
    emit_rom(ctx, "lizwiz_rom", ["6e.cpu", "6f.cpu", "6h.cpu", "6j.cpu", "wiza", "wizb"], "lizwiz_rom.h",
             compress=True)
    charmap(ctx, "lizwiz_tilemap", "5e.cpu", "lizwiz_tilemap.h", compress=True)
    spritemap(ctx, "lizwiz_sprites", "lizwiz", ["5f.cpu"], "lizwiz_spritemap.h", compress=True)
    cmap.colormap(ctx, "lizwiz_colormap", "4a.cpu", cmap.palette(ctx, "7f.cpu")[0:16], "lizwiz_cmap.h")


JOB = Job(name="lizwiz", convert=_convert, machine="lizwiz", zips=("lizwiz.zip",), deps=("pacman",))
