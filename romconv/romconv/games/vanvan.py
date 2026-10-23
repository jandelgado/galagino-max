from romconv.core.context import Context
from romconv.core.job import Job
from romconv.gfx import cmap
from romconv.gfx.rom import emit_rom
from romconv.gfx.sprites import spritemap
from romconv.gfx.tiles import charmap


def _convert(ctx: Context) -> None:
    # main bank 0x0000-0x3fff
    emit_rom(ctx, "vanvan_rom", ["van-1.50", "van-2.51", "van-3.52", "van-4.53"], "vanvan_rom.h", compress=True)
    # extra bank 0x8000-0x8fff
    emit_rom(ctx, "vanvan_rom2", ["van-5.39"], "vanvan_rom2.h", compress=True)
    charmap(ctx, "vanvan_tilemap", "van-20.18", "vanvan_tilemap.h", compress=True)
    spritemap(ctx, "vanvan_sprites", "pacman", ["van-21.19"], "vanvan_spritemap.h", compress=True)
    cmap.colormap(ctx, "vanvan_colormap", "6301-1.37", cmap.palette(ctx, "6331-1.6")[0:16], "vanvan_cmap.h")


JOB = Job(name="vanvan", convert=_convert, machine="vanvan", zips=("vanvan.zip",))
