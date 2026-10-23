from romconv.core.context import Context
from romconv.core.job import Job
from romconv.gfx import cmap
from romconv.gfx.rom import emit_rom
from romconv.gfx.sprites import spritemap
from romconv.gfx.tiles import charmap


def _convert(ctx: Context) -> None:
    emit_rom(ctx, "eyes_rom", ["d7", "e7", "f7", "h7"], "eyes_rom.h", decode=True, compress=True)
    charmap(ctx, "eyes_tilemap", "d5", "eyes_tilemap.h", compress=True)
    spritemap(ctx, "eyes_sprites", "eyes", ["e5"], "eyes_spritemap.h", compress=True)
    cmap.colormap(ctx, "eyes_colormap", "82s129.4a", cmap.palette(ctx, "82s123.7f")[0:16], "eyes_cmap.h")


JOB = Job(name="eyes", convert=_convert, machine="eyes", zips=("eyes.zip",), deps=("pacman",))
