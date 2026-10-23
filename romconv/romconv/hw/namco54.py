from romconv.core.context import Context
from romconv.core.job import Job
from romconv.gfx.rom import emit_rom


def _convert(ctx: Context) -> None:
    emit_rom(ctx, "namco54_rom", ["54xx.bin"], "namco54_rom.h", compress=True)


# the Namco 54xx sound MCU ROM, shared by every Namco machine that uses it
JOB = Job(name="namco54", convert=_convert, machine="namco54", zips=("galaga.zip",))
