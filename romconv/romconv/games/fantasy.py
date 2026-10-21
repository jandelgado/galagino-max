"""
   Convert the MAME Fantasy (US) ROM set into Galagino Plus headers.

   MAME 0.288 romset is fantasyu.zip - "Fantasy (US)"

"""
from types import MappingProxyType

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.hw.snk6502 import encode_samples, load, write_header, write_sample_pack

_SAMPLE_ZIP = "fantasy_samples.zip"  # samples/
_SAMPLE_NAMES = tuple(f"ft_voi-{x:x}.wav" for x in range(12))

_MANIFEST = MappingProxyType({
    "ic12.cpu":       (0x1000, 0x22CB2249),
    "ic07.cpu":       (0x1000, 0x0E2880B6),
    "ic08.cpu":       (0x1000, 0x4C331317),
    "ic09.cpu":       (0x1000, 0x6AC1DBFC),
    "ic10.cpu":       (0x1000, 0xC796A406),
    "ic14.cpu":       (0x1000, 0x6F1F0698),
    "ic15.cpu":       (0x1000, 0x5534D57E),
    "ic16.cpu":       (0x1000, 0x6C2AEB6E),
    "ic17.cpu":       (0x1000, 0xF6AA5DE1),
    "fs10ic50.bin":   (0x1000, 0x86A801C3),
    "fs11ic51.bin":   (0x1000, 0x9DFFF71C),
    "fantasy.ic7":    (0x0020, 0x361A5E99),
    "fantasy.ic6":    (0x0020, 0x33D974F7),
    "fs_b_51.bin":    (0x0800, 0x48094EC5),
    "fs_a_52.bin":    (0x0800, 0x1D0316E8),
    "fs_c_53.bin":    (0x0800, 0x49FD4AE8),
})


def _write_samples(ctx: Context) -> None:
    rates = [6000] * len(_SAMPLE_NAMES)
    data, offsets, lengths = encode_samples(ctx, _SAMPLE_ZIP, "Fantasy", _SAMPLE_NAMES, rates)
    write_sample_pack(ctx, "fantasy", "MAME Fantasy speech samples converted to mono 6 kHz 4-bit IMA-ADPCM.",
                      data, offsets, lengths)


def _convert(ctx: Context) -> None:
    program = b"".join(load(ctx, _MANIFEST, name) for name in (
        "ic12.cpu", "ic07.cpu", "ic08.cpu", "ic09.cpu", "ic10.cpu",
        "ic14.cpu", "ic15.cpu", "ic16.cpu", "ic17.cpu"))
    gfx = load(ctx, _MANIFEST, "fs10ic50.bin") + load(ctx, _MANIFEST, "fs11ic51.bin")
    proms = load(ctx, _MANIFEST, "fantasy.ic7") + load(ctx, _MANIFEST, "fantasy.ic6")
    sound = b"".join(load(ctx, _MANIFEST, name) for name in ("fs_b_51.bin", "fs_a_52.bin", "fs_c_53.bin"))
    write_header(ctx, "fantasy", "fantasy_rom.h", "fantasy_rom", program,
                  "Main M6502 ROMs mapped at 0x3000-0xbfff; IC14 is mirrored at 0xf000.")
    write_header(ctx, "fantasy", "fantasy_gfx.h", "fantasy_gfx", gfx,
                  "Fixed graphics: 512 characters, 8x8, 2 bpp, separated bitplanes.")
    write_header(ctx, "fantasy", "fantasy_proms.h", "fantasy_proms", proms,
                  "64-byte foreground/background palette PROMs.", compress=False)
    write_header(ctx, "fantasy", "fantasy_sound_rom.h", "fantasy_sound_rom", sound,
                  "Three 2 KiB music data ROMs used by the SNK 6502 sound sequencer.")
    _write_samples(ctx)
    ctx.log.info("Fantasy conversion complete.")


JOB = Job(name="fantasy", convert=_convert, machine="fantasy", zips=("fantasyu.zip",))
