from romconv.core.context import Context
from romconv.core.job import Job
from romconv.gfx import cmap
from romconv.gfx.rom import emit_rom
from romconv.gfx.sprites import spritemap
from romconv.gfx.tiles import charmap


def _convert(ctx: Context) -> None:
    emit_rom(ctx, "theglob_rom", ["glob.u2", "glob.u3"], "theglob_rom.h", compress=True)
    charmap(ctx, "theglob_tilemap", "glob.5e", "theglob_tilemap.h", compress=True)
    spritemap(ctx, "theglob_sprites", "pacman", ["glob.5f"], "theglob_spritemap.h", compress=True)
    cmap.colormap(ctx, "theglob_colormap", "glob.4a", cmap.palette(ctx, "glob.7f")[0:16], "theglob_cmap.h")


JOB = Job(name="theglob", convert=_convert, machine="theglob", zips=("theglobp.zip",), deps=("pacman",))
