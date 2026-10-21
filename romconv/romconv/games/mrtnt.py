from romconv.core.context import Context
from romconv.core.job import Job
from romconv.gfx import cmap
from romconv.gfx.rom import emit_rom
from romconv.gfx.sprites import spritemap
from romconv.gfx.tiles import charmap


def _convert(ctx: Context) -> None:
    emit_rom(ctx, "mrtnt_rom", ["tnt.1", "tnt.2", "tnt.3", "tnt.4"], "mrtnt_rom.h", decode=True, compress=True)
    charmap(ctx, "mrtnt_tilemap", "tnt.5", "mrtnt_tilemap.h", compress=True)
    spritemap(ctx, "mrtnt_sprites", "mrtnt", ["tnt.6"], "mrtnt_spritemap.h", compress=True)
    cmap.colormap(ctx, "mrtnt_colormap", "82s126.4a", cmap.palette(ctx, "82s123.7f")[0:16], "mrtnt_cmap.h")


JOB = Job(name="mrtnt", convert=_convert, machine="mrtnt", zips=("mrtnt.zip",), deps=("pacman",))
