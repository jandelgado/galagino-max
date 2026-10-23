"""Mr. Do! (MAME univers/docastle.cpp, mrdo)."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.games.mrdo.cpu import convert_cpu
from romconv.games.mrdo.palette import convert_palette, convert_sprite_colormap
from romconv.games.mrdo.sprites import convert_sprites
from romconv.games.mrdo.tiles import convert_bg_tiles, convert_fg_tiles


def _convert(ctx: Context) -> None:
    convert_cpu(ctx)
    convert_bg_tiles(ctx)
    convert_fg_tiles(ctx)
    convert_sprites(ctx)
    convert_palette(ctx)
    convert_sprite_colormap(ctx)


JOB = Job(name="mrdo", convert=_convert, machine="mrdo", zips=("mrdo.zip",))
