"""Convert the MAME asteroid (Asteroids rev 4) ROM set into galagino headers."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.hw.atari import convert_vector

# MAME asteroid: name -> SHA1. The DVG state PROM 034602-01.c8 is dropped:
# the machine decodes the display list at a high level.
_PROGRAM = (
    ("035145-04e.ef2", "5369dcfe01c0b9e48b15a96a0de8d23ee8ef9145"),
    ("035144-04e.h2", "51b2865fa897cdaa84ac6500c4b4833a80827019"),
    ("035143-02.j2", "1ce2eac1ab90b972e3f1fc3d250908f26328d6cb"),
)
_VECTOR = (("035127-02.np3", "8cd5005e531eafa361d6b7e9eed159d164776c70"),)


def _convert(ctx: Context) -> None:
    convert_vector(ctx, "asteroids", _PROGRAM, _VECTOR,
                   "Main M6502 ROM, 6 KiB mapped at 0x6800-0x7fff.",
                   "DVG vector ROM, 2 KiB mapped at 0x5000-0x57ff.")


JOB = Job(name="asteroids", convert=_convert, machine="asteroids", zips=("asteroid.zip",))
