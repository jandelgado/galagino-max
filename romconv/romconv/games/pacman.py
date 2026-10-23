from romconv.core.context import Context
from romconv.core.job import Job
from romconv.gfx import cmap
from romconv.gfx.rom import emit_rom
from romconv.gfx.sprites import spritemap
from romconv.gfx.tiles import charmap
from romconv.gfx.wavetable import wavetable


def _convert(ctx: Context) -> None:
    emit_rom(ctx, "pacman_rom", ["pacman.6e", "pacman.6f", "pacman.6h", "pacman.6j"], "pacman_rom.h",
             compress=True)
    charmap(ctx, "pacman_tilemap", "pacman.5e", "pacman_tilemap.h", compress=True)
    spritemap(ctx, "pacman_sprites", "pacman", ["pacman.5f"], "pacman_spritemap.h", compress=True)
    cmap.colormap(ctx, "pacman_colormap", "82s126.4a", cmap.palette(ctx, "82s123.7f")[0:16], "pacman_cmap.h")
    wavetable(ctx, "pacman_wavetable", ["82s126.1m"], "pacman_wavetable.h")


JOB = Job(name="pacman", convert=_convert, machine="pacman", zips=("pacman.zip",))
