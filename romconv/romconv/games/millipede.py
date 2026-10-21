"""Convert the MAME milliped (Millipede) ROM set into galagino headers."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.hw.atari import check_budget, load_all, write

# MAME milliped: name -> SHA1. Sync PROM 136001-213.e7 is not needed and
# dropped, as is the millipdd hack in the same archive.
_PROGRAM = (
    ("136013-104.mn1", "b595d6a0f5d3c611ade1b83a94c3b909d2124dc4"),
    ("136013-103.l1", "9c1d0bbc20bf25dd21761a311fd1ed80aa029241"),
    ("136013-102.jk1", "9fe40db55ba1d20d4f11704f7f5df9ff75b87f30"),
    ("136013-101.h1", "ab06b1fd80271849946f90757b3837b617394929"),
)
_GFX = (
    ("136013-107.r5", "4c7ea33d9501456ee8f5a642da7d6c972f2bb90d"),
    ("136013-106.p5", "602fcc7290f9f4eacb841c76665961ebf4307f80"),
)


def _convert(ctx: Context) -> None:
    total = write(ctx, "millipede", "millipede_rom.h", "millipede_rom", load_all(ctx, _PROGRAM),
                  "Main M6502 ROM, 16 KiB mapped at 0x4000-0x7fff.")
    total += write(ctx, "millipede", "millipede_gfx.h", "millipede_gfx", load_all(ctx, _GFX),
                   "Tiles/sprites, 2 bpp planar: LSB plane 0x000-0x7ff, MSB plane 0x800-0xfff.")
    # the machine builds 2 KiB tile + 8 KiB sprite column tables from gfx in
    # start(), each allocated with 3 bytes alignment slack
    total += 2048 + 3 + 8192 + 3
    check_budget(ctx, total)


JOB = Job(name="millipede", convert=_convert, machine="millipede", zips=("milliped.zip",))
