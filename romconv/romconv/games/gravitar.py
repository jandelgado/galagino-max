"""Convert the MAME gravitar ROM set into galagino headers."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.hw.atari import convert_vector

# MAME gravitar: name -> SHA1. Dropped: the AVG state PROM 136002-125.n4
# (the display list is decoded at a high level) and the address decoding
# PROMs 136010-111.r1 / 136010-112.r2.
_PROGRAM = (  # 0x9000-0xefff; 0xf000-0xffff repeats 306.m1
    ("136010-301.d1", "800b52ead9f56a3e372797fbc698c8fc791398da"),
    ("136010-302.ef1", "b9e846db14fa23f8d2def97030d8b072b2bbc0be"),
    ("136010-303.h1", "e09f58f6f36de6bf6724a1ab14ab35acbb0b3876"),
    ("136010-304.j1", "822b06be6f2d6298b2b10161fbabbb2caa74b2ef"),
    ("136010-305.kl1", "4a7124f91d3ee940686c51374a861efe6cb5d282"),
    ("136010-306.m1", "baf080deaa8eea43af2f3be71dacc63e4666c453"),
)
_VECTOR = (  # 0x2800-0x5fff
    ("136010-210.l7", "2c50cd38d60739c126f1d0d8e7fbd46a0bde6e1c"),
    ("136010-207.mn7", "301ddb7a34b38140a1fdffc060cb08ff57f10cf1"),
    ("136010-208.np7", "9c2920cf2b73a93ac2808be654b08505037f53b1"),
    ("136010-309.r7", "5164f2a54244ce1e863d1ec0dd29bc9da7103a85"),
)


def _convert(ctx: Context) -> None:
    convert_vector(ctx, "gravitar", _PROGRAM, _VECTOR,
                   "Main M6502 ROM, 24 KiB mapped at 0x9000-0xefff; 0xf000-0xffff mirrors 0xe000.",
                   "AVG vector ROM, 14 KiB mapped at 0x2800-0x5fff.",
                   program_bytes=0x6000, vector_bytes=0x3800)


JOB = Job(name="gravitar", convert=_convert, machine="gravitar", zips=("gravitar.zip",))
