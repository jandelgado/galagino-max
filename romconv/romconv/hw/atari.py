"""Helpers of the Atari vector game converters."""
from collections.abc import Sequence

from romconv.core.context import Context
from romconv.emit.write import write_bytes
from romconv.gfx.rom import rom_bytes_all
from romconv.plan.budget import check_budget

# dvgMachine::_MAX_LINES vec2d::Line (16 B) + StripRasterizer next/active
# (2 x uint16_t per line) + 36 strip heads, allocated in start()
_MAX_LINES = 1024
_LINE_BUFFER_BYTES = _MAX_LINES * 16 + _MAX_LINES * 2 * 2 + 36 * 2
# GO list: decode buffer (vec2d::Line per line) and published PackedLine
# (8 B) list (+ 2 B count + 4 B sequence)
_GO_LIST_BYTES = _MAX_LINES * 16 + _MAX_LINES * 8 + 8


def convert_vector(ctx: Context, game: str, program: Sequence[tuple[str, str]], vector: Sequence[tuple[str, str]],
                   program_description: str, vector_description: str, *, program_bytes: int | None = None,
                   vector_bytes: int | None = None) -> None:
    """Program ROM and DVG/AVG vector ROM headers of a vector game; the budget adds the
    machine's line buffers."""
    prog, vec = rom_bytes_all(ctx, program), rom_bytes_all(ctx, vector)
    if program_bytes is not None:
        assert len(prog) == program_bytes and len(vec) == vector_bytes
    total = write_bytes(ctx, f"{game}_rom.h", f"{game}_rom", prog, program_description, __file__)
    total += write_bytes(ctx, f"{game}_vrom.h", f"{game}_vrom", vec, vector_description, __file__)
    total += _LINE_BUFFER_BYTES + _GO_LIST_BYTES
    check_budget(ctx, total)
