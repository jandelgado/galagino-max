"""Arena budget checks of the converters that know their machine's other Arena users.

The capacity is the smallest pool the game is built for: every [arena] row for a
catalog game (it also runs as a remote build), the local rows otherwise. Unplanned
games allocate ARENA_ANY at run time, so the total is the check, not the split."""
from collections.abc import Mapping

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.plan.layout import config_path, min_capacity, read_layouts
from romconv.remote.catalog import read_games


def _capacity(ctx: Context) -> int:
    game = ctx.job.machine or ctx.job.name
    remote = read_games(ctx.paths).get(game, False)
    return min_capacity(read_layouts(config_path(ctx.paths)), remote)


def check_budget(ctx: Context, total: int) -> None:
    cap = _capacity(ctx)
    ctx.log.info(f"  Arena budget: {total} / {cap} bytes")
    if total >= cap:
        raise ConversionError(f"Arena budget exceeded: {total} >= {cap}")


def log_sizes(ctx: Context, sizes: Mapping[str, int]) -> None:
    """Log every resident's size and the total, ConversionError if the total reaches the capacity."""
    for name, size in sizes.items():
        ctx.log.info(f"  {name:8s} {size:6d}")
    total = sum(sizes.values())
    cap = _capacity(ctx)
    ctx.log.info(f"Arena total {total} B (budget {cap})")
    if total >= cap:
        raise ConversionError(f"Arena assets exceed the budget: {total} >= {cap}")
