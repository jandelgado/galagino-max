"""
   Convert the MAME Vanguard ROM set into Galagino Plus headers.

"""
from types import MappingProxyType

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.hw.snk6502 import encode_samples, load, write_header, write_sample_pack

_SAMPLE_ZIP = "vanguard_samples.zip"  # samples/
_SAMPLE_NAMES = tuple(["fire.wav", "explsion.wav"] + [f"vg_voi-{x:x}.wav" for x in range(16)])

# MAME `vanguard` parent set: filename -> (size, CRC32).
_MANIFEST = MappingProxyType({
    "sk4_ic07.bin": (0x1000, 0x6A29E354),
    "sk4_ic08.bin": (0x1000, 0x302BBA54),
    "sk4_ic09.bin": (0x1000, 0x424755F6),
    "sk4_ic10.bin": (0x1000, 0x54603274),
    "sk4_ic13.bin": (0x1000, 0xFDE157D0),
    "sk4_ic14.bin": (0x1000, 0x0D5B47D0),
    "sk4_ic15.bin": (0x1000, 0x8549B8F8),
    "sk4_ic16.bin": (0x1000, 0x062E0BE2),
    "sk5_ic50.bin": (0x0800, 0xE7D4315B),
    "sk5_ic51.bin": (0x0800, 0x96E87858),
    "sk5_ic7.bin":  (0x0020, 0xAD782A73),
    "sk5_ic6.bin":  (0x0020, 0x7DC9D450),
    "sk4_ic51.bin": (0x0800, 0xD2A64006),
    "sk4_ic52.bin": (0x0800, 0xCC4A0B6F),
})


def _write_samples(ctx: Context) -> None:
    rates = [8000, 8000] + [6000] * 16
    data, offsets, lengths = encode_samples(ctx, _SAMPLE_ZIP, "Vanguard", _SAMPLE_NAMES, rates)
    dividers = ",".join(str(24000 // rate) for rate in rates)
    write_sample_pack(ctx, "vanguard",
                      "MAME Vanguard samples converted to mono 4-bit IMA-ADPCM (effects 8 kHz, speech 6 kHz).",
                      data, offsets, lengths,
                      "const unsigned char vanguard_sample_dividers[] = {\n  " + dividers + "\n};\n")


def _convert(ctx: Context) -> None:
    def rom(name):
        return load(ctx, _MANIFEST, name)

    program = (rom("sk4_ic07.bin") + rom("sk4_ic08.bin") + rom("sk4_ic09.bin") + rom("sk4_ic10.bin") +
               rom("sk4_ic13.bin") + rom("sk4_ic14.bin") + rom("sk4_ic15.bin") + rom("sk4_ic16.bin"))
    gfx = rom("sk5_ic50.bin") + rom("sk5_ic51.bin")
    proms = rom("sk5_ic7.bin") + rom("sk5_ic6.bin")
    sound = rom("sk4_ic51.bin") + rom("sk4_ic52.bin")
    write_header(ctx, "vanguard", "vanguard_rom.h", "vanguard_rom", program,
                 "Main M6502 ROM: eight 4 KiB ROMs mapped at 0x4000-0xbfff; IC13 is mirrored at 0xf000.")
    write_header(ctx, "vanguard", "vanguard_gfx.h", "vanguard_gfx", gfx,
                 "Fixed character graphics: 256 characters, 8x8, 2 bpp, separated bitplanes.")
    write_header(ctx, "vanguard", "vanguard_proms.h", "vanguard_proms", proms,
                 "64-byte palette: foreground PROM followed by background PROM.", compress=False)
    write_header(ctx, "vanguard", "vanguard_sound_rom.h", "vanguard_sound_rom", sound,
                 "Two 2 KiB music data ROMs used by the SNK 6502 sound sequencer.")
    _write_samples(ctx)
    ctx.log.info("Vanguard conversion complete.")


JOB = Job(name="vanguard", convert=_convert, machine="vanguard", zips=("vanguard.zip",))
