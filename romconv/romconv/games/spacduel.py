"""Convert the MAME spacduel (Space Duel version 2) ROM set into galagino headers."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.hw.atari import convert_vector

# MAME spacduel: name -> SHA1. The AVG state PROM 136002-125.n4 is dropped:
# the display list is decoded at a high level.
_PROGRAM = (
    ("136006-201.r1", "9bacb64d257edd31f53db878477604f50681d78f"),
    ("136006-102.np1", "c05c52bb08acccb60950a15f05c960c3bc163d3e"),
    ("136006-103.m1", "d36d62cdf7fe76ee9cdbfc2e76ac5d90f22986ba"),
    ("136006-104.kl1", "9e8773e78d65d74db824cfd7108e7038f26757db"),
    ("136006-105.j1", "b15891d22a47ac3448d2ced40c04d0ab80606c7d"),
)
_VECTOR = (
    ("136006-106.r7", "f53be76a49dba319050ca7767de3441521910e83"),
    ("136006-107.np7", "58060b20b2511d30d2ec06479d21840bdd0b53c6"),
)


def _convert(ctx: Context) -> None:
    convert_vector(ctx, "spacduel", _PROGRAM, _VECTOR,
                   "Main M6502 ROM, 20 KiB mapped at 0x4000-0x8fff; 0x9000-0xffff mirrors 0x8000.",
                   "AVG vector ROM, 6 KiB mapped at 0x2800-0x3fff.")


JOB = Job(name="spacduel", convert=_convert, machine="spacduel", zips=("spacduel.zip",))
