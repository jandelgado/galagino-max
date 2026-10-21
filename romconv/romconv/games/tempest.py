"""Convert the MAME tempest (Tempest rev 3, revised hardware) ROM set into galagino headers."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.hw.atari import convert_vector

# MAME tempest: name -> SHA1. The AVG state PROM 136002-125.d7 and the
# Mathbox PROMs 136002-126..132 are dropped: display list and Mathbox are
# emulated at a high level.
_PROGRAM = (
    ("136002-133.d1", "7bef95db9b1102d6b1166bda0ccb276ef4cc3764"),
    ("136002-134.f1", "89144baf1efc703b2336774793ce345b37829ee7"),
    ("136002-235.j1", "a5f5fb630a48c5d25346f90d4c13aaa98f60b228"),
    ("136002-136.lm1", "73aa7d6f4e7093ccb2d97f6344f354872bcfd72a"),
    ("136002-237.p1", "04be074e45bf5cd95a852af97cd04e35b7f27fc4"),
)
_VECTOR = (("136002-138.np3", "2b725ee1a57d423c7d7377a1744f48412e0f2f69"),)


def _convert(ctx: Context) -> None:
    convert_vector(ctx, "tempest", _PROGRAM, _VECTOR,
                   "Main M6502 ROM, 20 KiB mapped at 0x9000-0xdfff; 0xf000-0xffff mirrors 0xd000.",
                   "AVG vector ROM, 4 KiB mapped at 0x3000-0x3fff.")


JOB = Job(name="tempest", convert=_convert, machine="tempest", zips=("tempest.zip",))
