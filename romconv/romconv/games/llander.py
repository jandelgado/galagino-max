"""Convert the MAME llander (Lunar Lander rev 2) ROM set into galagino headers."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.hw.atari import convert_vector

# MAME llander: name -> SHA1, in address order. The DVG state PROM
# 034602-01.c8 is dropped: the display list is decoded at a high level.
_PROGRAM = (  # 0x6000-0x7fff
    ("034572-02.f1", "5a15eaeaf825ccdf9ce013a6789cf51da20f785c"),
    ("034571-02.de1", "4be6cef5af38734d580cbfb7e4070fe7981ddfd6"),
    ("034570-01.c1", "ecf4430a0040c227c896aa2cd81ee03960b4d641"),
    ("034569-02.b1", "9b21ba5e1518079c326ca6e15b9993e6c4483caa"),
)
_VECTOR = (  # 0x4800-0x5fff
    ("034599-01.r3", "6ecb40169b797d9eb623bcb17872f745b1bf20fa"),
    ("034598-01.np3", "eb4ffc289d254f699f821df3146aa2c6cd78597f"),
    ("034597-01.m3", "e685b094c1261a351e4e82dfb487462163f136a4"),
)


def _convert(ctx: Context) -> None:
    convert_vector(ctx, "llander", _PROGRAM, _VECTOR,
                   "Main M6502 ROM, 8 KiB mapped at 0x6000-0x7fff.",
                   "DVG vector ROM, 6 KiB mapped at 0x4800-0x5fff.")


JOB = Job(name="llander", convert=_convert, machine="llander", zips=("llander.zip",))
