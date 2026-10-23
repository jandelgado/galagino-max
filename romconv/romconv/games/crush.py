from romconv.core.context import Context
from romconv.core.job import Job
from romconv.gfx import cmap
from romconv.gfx.rom import emit_rom
from romconv.gfx.sprites import spritemap
from romconv.gfx.tiles import charmap


def _convert(ctx: Context) -> None:
    emit_rom(ctx, "crush_rom", ["crushkrl.6e", "crushkrl.6f", "crushkrl.6h", "crushkrl.6j"], "crush_rom.h",
             compress=True)
    charmap(ctx, "crush_tilemap", "maketrax.5e", "crush_tilemap.h", compress=True)
    spritemap(ctx, "crush_sprites", "crush", ["maketrax.5f"], "crush_spritemap.h", compress=True)
    cmap.colormap(ctx, "crush_colormap", "2s140.4a", cmap.palette(ctx, "82s123.7f")[0:16], "crush_cmap.h")


JOB = Job(name="crush", convert=_convert, machine="crush", zips=("crush.zip",), deps=("pacman",))
