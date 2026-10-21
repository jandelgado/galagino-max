"""Convert the MAME bzone (Battle Zone rev 2) ROM set into galagino headers."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.hw.atari import convert_vector

# MAME bzone: name -> SHA1. The AVG state PROM 036408-01.k7 and the
# Mathbox PROMs are dropped: display list and Mathbox are emulated at a
# high level.
_PROGRAM = (
    ("036414-02.e1", "40e356ddc5c042bc1ce0b71f51e8b6de72daf1e4"),
    ("036413-01.h1", "42638cff53a9791a0f18d316f62a0ea8eea4e194"),
    ("036412-01.j1", "6bbb8316d9f8588ea0893932f9174788292b8edc"),
    ("036411-01.k1", "54c5e06b2e69eb731a6c9b1704e4340f493e7ea5"),
    ("036410-01.lm1", "33ae0f68b4e2eae9f3aecbee2d0b29003ce460b2"),
    ("036409-01.n1", "448fab30535e6fad7e0ab4427bc06bbbe075e797"),
)
_VECTOR = (
    ("036422-01.bc3", "147d97a3b475e738ce00b1a7909bbd787ad06eda"),
    ("036421-01.a3", "b71e0ab0e220c3e64dc2b094c701fb1a960b64e4"),
)


def _convert(ctx: Context) -> None:
    convert_vector(ctx, "bzone", _PROGRAM, _VECTOR,
                   "Main M6502 ROM, 12 KiB mapped at 0x5000-0x7fff.",
                   "AVG vector ROM, 4 KiB mapped at 0x3000-0x3fff.")


JOB = Job(name="bzone", convert=_convert, machine="bzone", zips=("bzone.zip",))
