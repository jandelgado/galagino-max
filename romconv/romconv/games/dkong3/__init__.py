"""Donkey Kong 3, Japanese set (MAME dkong.cpp, dkong3j)."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.games.dkong3.cmap import convert_cmap, convert_color_codes
from romconv.games.dkong3.cpu import convert_cpu, convert_sound
from romconv.games.dkong3.gfx import convert_sprites, convert_tilemap


def _convert(ctx: Context) -> None:
    convert_cpu(ctx)
    convert_sound(ctx)
    convert_tilemap(ctx)
    convert_cmap(ctx)
    convert_color_codes(ctx)
    convert_sprites(ctx)


JOB = Job(name="dkong3", convert=_convert, machine="dkong3", zips=("dkong3.zip",))
