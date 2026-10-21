from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from romconv.core.context import Context


@dataclass(frozen=True)
class Job:
    """One unit of conversion work, addressed by name.

    deps are pulled into a partial run; they do not order jobs of one phase.
    phase 2 jobs run in-process after all phase 1 jobs succeeded."""

    name: str
    convert: Callable[[Context], None]
    machine: str | None = None
    zips: tuple[str, ...] = ()
    plan: bool = False
    deps: tuple[str, ...] = ()
    phase: int = 1
