"""Convert the MAME centiped3 (Centipede revision 3) ROM set into galagino headers."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.hw.atari import check_budget, load_all, write

# MAME centiped3: name -> SHA1. PROM 136001-213.p4 is unused by the video
# hardware and dropped.
_PROGRAM = (
    ("136001-307.d1", "8ea6e3304202831aabaf31dbd0f970a7b3bfe421"),
    ("136001-308.e1", "af4fdbf32c23b1864819d620a874e7f205da3cdb"),
    ("136001-309.fh1", "689fa560d40a384dcbcad7c8095bc12e91875580"),
    ("136001-310.j1", "c557db83876afc8ab52047ab1a3c3bfef34d6351"),
)
_GFX = (
    ("136001-211.f7", "6c862352c329776f2f9974a0df9dbe41f9dbc361"),
    ("136001-212.hj7", "974c03d29aeca672fffa4dfc00a06be6a851aacb"),
)


def _convert(ctx: Context) -> None:
    total = write(ctx, "centipede", "centipede_rom.h", "centipede_rom", load_all(ctx, _PROGRAM),
                  "Main M6502 ROM, 8 KiB mapped at 0x2000-0x3fff.")
    total += write(ctx, "centipede", "centipede_gfx.h", "centipede_gfx", load_all(ctx, _GFX),
                   "Tiles/sprites, 2 bpp planar: LSB plane 0x000-0x7ff, MSB plane 0x800-0xfff.")
    # the machine builds 2 KiB tile + 8 KiB sprite column tables from gfx in start()
    total += 2048 + 8192
    check_budget(ctx, total)


JOB = Job(name="centipede", convert=_convert, machine="centipede", zips=("centiped.zip",))
