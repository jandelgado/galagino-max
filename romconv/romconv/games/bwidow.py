"""Convert the MAME bwidow (Black Widow) ROM set into galagino headers."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.hw.atari import convert_vector

# MAME bwidow: name -> SHA1. Dropped: the AVG state PROM 136002-125.n4
# (the display list is decoded at a high level).
_PROGRAM = (  # 0x9000-0xefff; 0xf000-0xffff repeats 106.m1
    ("136017-101.d1", "b62f7622ca60248e1b8376ee135ae3d94d0b4437"),
    ("136017-102.ef1", "614c74daa468a7430ed965a3a9d07b6ad846016c"),
    ("136017-103.h1", "3aa6c40721a4289c1cf01f37c89b6b0a96336c68"),
    ("136017-104.j1", "e83d8242e4592149719be6a68cf3aba46116072f"),
    ("136017-105.kl1", "33da2ba3cefa3d0dddc8647f9b6caf5d5bfe9b3b"),
    ("136017-106.m1", "f1398e3ff2b62af1509bc117028845b671ff1ca2"),
)
_VECTOR = (  # 0x2800-0x5fff
    ("136017-107.l7", "bbae93058228820ee67b05f23e45fb54ee0963ff"),
    ("136017-108.mn7", "935295d66ad40ad702eb7a694296e836f53d22ec"),
    ("136017-109.np7", "2b324877bf55151747eaacd9a58f846712bfbc14"),
    ("136017-110.r7", "72aa1d24f20cc86701189df486488edc434b1be1"),
)


def _convert(ctx: Context) -> None:
    convert_vector(ctx, "bwidow", _PROGRAM, _VECTOR,
                   "Main M6502 ROM, 24 KiB mapped at 0x9000-0xefff; 0xf000-0xffff mirrors 0xe000.",
                   "AVG vector ROM, 14 KiB mapped at 0x2800-0x5fff.",
                   program_bytes=0x6000, vector_bytes=0x3800)


JOB = Job(name="bwidow", convert=_convert, machine="bwidow", zips=("bwidow.zip",))
