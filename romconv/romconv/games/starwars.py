"""Convert the MAME starwars (Star Wars set 1) ROM set into galagino headers."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.write import write_bytes
from romconv.gfx.rom import rom_bytes_all
from romconv.plan.budget import check_budget

# MAME starwars: name -> SHA1. The AVG state PROM 136021-109.4b is dropped:
# the display list is decoded at instruction level.
_ROM0 = ("136021.214.1f", "c1d3637cb31ece0890c25f6122d6bcd27e6ffe0c")  # 16 KiB: bank pages 0, 1
_PROGRAM = (
    ("136021.102.1hj", "f8943b67f2ea032ab9538084756ba86f892be5ca"),  # 0x8000
    ("136021.203.1jk", "dd53b643be856787bbc4da63e5eb132f98f623c3"),  # 0xa000
    ("136021.104.1kl", "981b505d6e06d7149f8bcb3e81e4d0c790f2fc86"),  # 0xc000
    ("136021.206.1m", "4960f4446271316e3f730eeb2531dbc702947395"),  # 0xe000
)
_VECTOR = (("136021-105.1l", "032c933fd94a6b0b294beee29159a24494ae969b"),)
_SOUND = (
    ("136021-107.1jk", "c38661b2b846fe93487eef09ca3cda19c44f08a0"),  # 0x4000, 0xc000
    ("136021-208.1h", "c858ae1702efdd48615453ab46e488848891d139"),  # 0x6000, 0xe000
)
# matrix processor microcode, PROM 0 holds the most significant nibble
_MATH_PROMS = (
    ("136021-110.7h", "d247cbb0afb4538d5161f8ce9eab337cdb3f2da4"),
    ("136021-111.7j", "f3420c6e15602956fd94982a5d8d4ddd015ed977"),
    ("136021-112.7k", "4dcf5153221feca329b8e8d199bd4fc00b151d9c"),
    ("136021-113.7l", "e13541b09d1724204fdb171528e9a1c83c799c1c"),
)

# Other Arena users, allocated in starwars::start(); keep in sync:
# - lines: starwars::MAX_LINES vec2d::Line (16 B) + StripRasterizer
#   next/active (2 x uint16_t per line) + 36 strip heads
# - GO list: decode buffer (vec2d::Line per line) and published
#   PackedLine (8 B) list (+ 2 B count + 4 B sequence)
# - Math RAM 0x5000-0x5fff
_MAX_LINES = 2048
_LINE_BUFFER_BYTES = _MAX_LINES * 16 + _MAX_LINES * 2 * 2 + 36 * 2
_GO_LIST_BYTES = _MAX_LINES * 16 + _MAX_LINES * 8 + 8
_MATH_RAM_BYTES = 0x1000


def _convert(ctx: Context) -> None:
    rom0 = bytes(ctx.rom(*_ROM0))
    program = rom0[:0x2000] + rom_bytes_all(ctx, _PROGRAM) + rom0[0x2000:]
    total = write_bytes(ctx, "starwars_rom.h", "starwars_rom", program,
                        "Main M6809 ROM: 0x6000-0xffff (bank page 0 at 0x6000), then bank page 1.", "starwars")
    total += write_bytes(ctx, "starwars_vrom.h", "starwars_vrom", rom_bytes_all(ctx, _VECTOR),
                         "AVG vector ROM, 4 KiB mapped at 0x3000-0x3fff.", "starwars")
    total += write_bytes(ctx, "starwars_srom.h", "starwars_srom", rom_bytes_all(ctx, _SOUND),
                         "Sound M6809 ROM, 16 KiB mapped at 0x4000-0x7fff and 0xc000-0xffff.", "starwars")

    proms = [bytes(ctx.rom(name, sha1)) for name, sha1 in _MATH_PROMS]
    micro = [(proms[0][i] & 0xf) << 12 | (proms[1][i] & 0xf) << 8 | (proms[2][i] & 0xf) << 4 |
             (proms[3][i] & 0xf) for i in range(0x400)]
    total += write_bytes(ctx, "starwars_mproms.h", "starwars_mproms", micro,
                         "Matrix processor microcode, 1024 words: strobes 15-8, AM 7, address 6-0.", "starwars",
                         ctype="unsigned short")

    total += _LINE_BUFFER_BYTES + _GO_LIST_BYTES + _MATH_RAM_BYTES
    check_budget(ctx, total)


JOB = Job(name="starwars", convert=_convert, machine="starwars", zips=("starwars.zip",))
