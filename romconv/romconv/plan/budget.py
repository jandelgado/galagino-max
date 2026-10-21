"""Arena budget checks of the converters that know their machine's other Arena users."""
from collections.abc import Mapping

from romconv.core.context import Context
from romconv.core.errors import ConversionError

_ARENA_CAPACITY = 182000


def check_budget(ctx: Context, total: int, capacity: int = _ARENA_CAPACITY) -> None:
    ctx.log.info(f"  Arena budget: {total} / {capacity} bytes")
    if total >= capacity:
        raise ConversionError("Arena budget exceeded")


def log_sizes(ctx: Context, sizes: Mapping[str, int], capacity: int = _ARENA_CAPACITY) -> None:
    """Log every resident's size and the total, ConversionError if the total reaches the capacity."""
    for name, size in sizes.items():
        ctx.log.info(f"  {name:8s} {size:6d}")
    total = sum(sizes.values())
    ctx.log.info(f"Arena total {total} B (budget {capacity})")
    if total >= capacity:
        raise ConversionError(f"Arena assets exceed the budget: {total} >= {capacity}")
