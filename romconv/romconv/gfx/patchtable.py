"""Per-position delta tables (`*.table`, one hex value per line) that patch converted asset values."""
from collections.abc import Sequence
from pathlib import Path

from romconv.core.errors import ConversionError


def read_patch_table(path: Path) -> list[int]:
    """The delta values of a table file, in file order."""
    try:
        lines = [line.strip() for line in path.read_text().splitlines() if line.strip()]
        return [int(line, 16) for line in lines]
    except (OSError, ValueError) as e:
        raise ConversionError(f"patch table {path.name}: {e}") from e


def apply_patch_values(values: Sequence[int], patch_values: Sequence[int], mask: int) -> list[int]:
    """values + delta per position (non-zero deltas only), masked to the value type's bits.
    The longer of the two sequences is cut off at the shorter one for the patch only."""
    out = list(values)
    for i in range(min(len(out), len(patch_values))):
        if patch_values[i] != 0:
            out[i] = (out[i] + patch_values[i]) & mask
    return out
