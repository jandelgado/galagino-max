"""Bomb Jack (MAME tehkan/bombjack.cpp)."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.games.bombjack.cpu import convert_audio_cpu, convert_bg_maps, convert_main_cpu
from romconv.games.bombjack.sprites import convert_sprites
from romconv.games.bombjack.tiles import convert_bg_tiles, convert_fg_tiles


def _convert(ctx: Context) -> None:
    convert_main_cpu(ctx)
    convert_audio_cpu(ctx)
    convert_bg_tiles(ctx)
    convert_fg_tiles(ctx)
    convert_bg_maps(ctx)
    convert_sprites(ctx)


JOB = Job(name="bombjack", convert=_convert, machine="bombjack", zips=("bombjack.zip",), plan=True)
