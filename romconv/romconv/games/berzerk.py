"""Convert the MAME berzerk (RC31A) ROM set into galagino headers."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.write import write_bytes
from romconv.gfx.rom import rom_bytes_all
from romconv.plan.budget import check_budget

# MAME berzerk: (name, Z80 address, SHA1); 0x0800-0x0fff is NVRAM, 0x3800 unpopulated
_PROGRAM = (
    ("berzerk_rc31_1c.rom0.1c", 0x0000, "fae2647f12f1cd82826db61b53b116a5e0c9f995"),
    ("berzerk_rc31_1d.rom1.1d", 0x1000, "69af170c4a39a3494dcd180737e5c87b455f9203"),
    ("berzerk_rc31_3d.rom2.3d", 0x1800, "a0b7842f6a5f86c16d80d78e7012c78b3ea11d1d"),
    ("berzerk_rc31_5d.rom3.5d", 0x2000, "07f849aa39f1e3db938187ffde4a46a588156ddc"),
    ("berzerk_rc31_6d.rom4.6d", 0x2800, "5a5e549ec0e4803ab2d1eac6b3e7171aedf28244"),
    ("berzerk_rc31a_5c.rom5.5c", 0x3000, "31acef9583546671debe768e3d5c695ba1b9f7e0"),
)
_SPEECH = (
    ("berzerk_r_vo_1c.1c", "f12fed8712f20fa8213f606c4049a8144bfea42e"),
    ("berzerk_r_vo_2c.2c", "20a6611ad6ec19409ac138bdae7bdfaeab6c47cf"),
)
# Arena: two VideoState copies (224 x 32 bitmap + 56 x 32 color)
_VIDEO_STATE_BYTES = 2 * (224 * 32 + 56 * 32)


def _convert(ctx: Context) -> None:
    rom = bytearray([0xff]) * 0x4000
    for name, addr, sha1 in _PROGRAM:
        rom[addr:addr + 0x800] = ctx.rom(name, sha1)
    speech = rom_bytes_all(ctx, _SPEECH)
    total = write_bytes(ctx, "berzerk_rom.h", "berzerk_rom", rom,
                        "Z80 ROM image 0x0000-0x3fff (0x0800-0x0fff NVRAM hole, 0x3800 unpopulated).",
                        "berzerk")
    total += write_bytes(ctx, "berzerk_speech.h", "berzerk_speech", speech, "S14001A speech ROM, 4 KiB.",
                         "berzerk")
    total += _VIDEO_STATE_BYTES
    check_budget(ctx, total)


JOB = Job(name="berzerk", convert=_convert, machine="berzerk", zips=("berzerk.zip",))
