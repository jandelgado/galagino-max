"""Donkey Kong Junior, Japanese set (MAME dkong.cpp, dkongjrj)."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.games.dkongjr.cmap import convert_cmap
from romconv.games.dkongjr.cpu import convert_cpu, convert_sound
from romconv.games.dkongjr.gfx import convert_sprites, convert_tilemap


def _convert(ctx: Context) -> None:
    convert_cpu(ctx)
    convert_sound(ctx)
    convert_tilemap(ctx)
    convert_cmap(ctx)
    convert_sprites(ctx)


JOB = Job(name="dkongjr", convert=_convert, machine="dkongjr", zips=("dkongjrj.zip",), deps=("dkong",))
