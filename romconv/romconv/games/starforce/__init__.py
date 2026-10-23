"""Star Force and Baluba-louk no Densetsu (MAME tehkan/senjyo.cpp): one board, two ROM sets."""
from functools import partial

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.games.starforce.cpu import convert_cpu, convert_palette
from romconv.games.starforce.sets import SETS
from romconv.games.starforce.sprites import convert_sprites
from romconv.games.starforce.tiles import convert_bg_tiles, convert_fg_tiles


def _convert(game: str, ctx: Context) -> None:
    spec = SETS[game]
    convert_cpu(ctx, game, spec)
    convert_bg_tiles(ctx, game, spec)
    convert_fg_tiles(ctx, game, spec)
    convert_sprites(ctx, game, spec)
    # only Star Force has the palette header
    if game == "starforce":
        convert_palette(ctx)


# baluba constructs the Star Force assets too (catalog.BASES)
JOBS = (
    Job(name="starforce", convert=partial(_convert, "starforce"), machine="starforce",
        zips=(SETS["starforce"].zip,), plan=True),
    Job(name="baluba", convert=partial(_convert, "baluba"), machine="baluba",
        zips=(SETS["baluba"].zip,), plan=True, deps=("starforce",)),
)
