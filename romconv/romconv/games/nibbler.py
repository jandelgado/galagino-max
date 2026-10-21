"""
   Convert the supplied Rock-Ola Nibbler ROM set into Galagino Plus headers.

   ROMSET from MAME 0.288 names

   nibblerp.zip - "Nibbler (rev 6, Pioneer Balloon conversion)"

"""
from types import MappingProxyType

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.hw.snk6502 import load, write_header

# mame 0.288 - nibblerp /* revision 6 + extra soundrom */
_MANIFEST = MappingProxyType({
    "ic12":           (0x1000, 0xAC6A802B),  # main cpu
    "ic7":            (0x1000, 0x35971364),
    "ic8":            (0x1000, 0x6B33B806),
    "ic9":            (0x1000, 0x91A4F98D),
    "ic10":           (0x1000, 0xA151D934),
    "ic14":           (0x1000, 0x063F05CC),
    "g-0960-54.ic15": (0x1000, 0x7205FB8D),
    "g-0960-55.ic16": (0x1000, 0x4BB39815),
    "g-0960-56.ic17": (0x1000, 0xED680F19),
    "g-0960-57.ic50": (0x1000, 0x01D4D0C2),  # gfx1
    "g-0960-58.ic51": (0x1000, 0xFEFF7FAF),  # gfx1
    "g-0708-05.ic7":  (0x0020, 0xA5709FF3),  # proms - foreground colors
    "g-0708-04.ic6":  (0x0020, 0xDACD592D),  # proms - background colors
    "sk7_ic51.bin":   (0x0800, 0x0345F8B7),  # snk6502 sound proms - Rom from Pioneer Balloon
    "g-0959-44.ic52": (0x0800, 0x87D67DEE),  # snk6502 sound proms
    "g-0959-45.ic53": (0x0800, 0x33189917),  # snk6502 sound proms
})


def _convert(ctx: Context) -> None:
    program = b"".join(load(ctx, _MANIFEST, name) for name in (
        "ic12", "ic7", "ic8", "ic9", "ic10", "ic14", "g-0960-54.ic15", "g-0960-55.ic16", "g-0960-56.ic17"))
    gfx = load(ctx, _MANIFEST, "g-0960-57.ic50") + load(ctx, _MANIFEST, "g-0960-58.ic51")
    proms = load(ctx, _MANIFEST, "g-0708-05.ic7") + load(ctx, _MANIFEST, "g-0708-04.ic6")
    sound = b"".join(load(ctx, _MANIFEST, name) for name in ("sk7_ic51.bin", "g-0959-44.ic52", "g-0959-45.ic53"))
    write_header(ctx, "nibbler", "nibbler_rom.h", "nibbler_rom", program,
                 "Revision 6 main M6502 ROMs at 0x3000-0xbfff; IC14 is mirrored at 0xf000.")
    write_header(ctx, "nibbler", "nibbler_gfx.h", "nibbler_gfx", gfx,
                 "Fixed graphics: 512 characters, 8x8, 2 bpp, separated bitplanes.")
    write_header(ctx, "nibbler", "nibbler_proms.h", "nibbler_proms", proms,
                 "64-byte foreground/background palette PROMs.", compress=False)
    write_header(ctx, "nibbler", "nibbler_sound_rom.h", "nibbler_sound_rom", sound,
                 "Three 2 KiB music ROMs used by the SNK 6502 sound sequencer.")
    ctx.log.info("Nibbler conversion complete.")


JOB = Job(name="nibbler", convert=_convert, machine="nibbler", zips=("nibblerp.zip",))
