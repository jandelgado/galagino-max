"""Pengo (MAME sega/pengo.cpp)."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.games.pengo.colormap import convert_colormap
from romconv.games.pengo.cpu import convert_cpu, convert_wavetable
from romconv.games.pengo.gfx import convert_sprites, convert_tiles


def _convert(ctx: Context) -> None:
    convert_cpu(ctx)
    convert_wavetable(ctx)
    convert_tiles(ctx)
    convert_sprites(ctx)
    convert_colormap(ctx)


# MAME pengo.zip, the unencrypted sets: "pengoj" (Japan, Popcorn) preferred over "pengo" (World);
# clone sets live in subdirectories.
JOB = Job(name="pengo", convert=_convert, machine="pengo", zips=("pengo.zip",), deps=("pacman",))
