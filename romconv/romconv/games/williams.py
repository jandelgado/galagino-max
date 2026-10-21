"""Williams gen 1 (MAME williams/williams.cpp): defender, robotron, joust, sinistar.

CPU ROMs as LZMA assets, planned. The shared 256 entry RGB565 palette (palette_init: R/G
resistors 1200/560/330, B 560/330, BBGGGRRR) is written once by the williams_palette job."""
from functools import partial

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed, emit_plain
from romconv.gfx.color import rgb565_swapped
from romconv.gfx.resnet import combine, resistor_weights

_PAGE = 0x800
_PALETTE_JOB = "williams_palette"

_ROBOTRON = (("2084_rom_1b_3005-13.e4", "f6d60e26c209c1df2cc01ac07ad5559daa1b7118"),
             ("2084_rom_2b_3005-14.c4", "4d6e82bc29f49100f7751ccfc6a9ff35695b84b3"),
             ("2084_rom_3b_3005-15.a4", "06a8c8dd0b4726eb7f0bb0e89c8533931d75fc1c"),
             ("2084_rom_4b_3005-16.e5", "aaf89c19fd8f4e8750717169eb1af476aef38a5e"),
             ("2084_rom_5b_3005-17.c5", "79b4680ce19bd28882ae823f0e7b293af17cbb91"),
             ("2084_rom_6b_3005-18.a5", "f76ec5432a7939b33a27be1c6855e2dbe6d9fdc8"),
             ("2084_rom_7b_3005-19.e6", "06eae5138254723819a5e93cfd9e9f3285fcddf5"),
             ("2084_rom_8b_3005-20.c6", "7ae38a609ed9a6f62ca003cab719740ed7651b7c"),
             ("2084_rom_9b_3005-21.a6", "fd9d75b866f0ebbb723f84889337e6814496a103"),
             ("2084_rom_10b_3005-22.a7", "d426a50e75dabe936de643c83a548da5e399331c"),
             ("2084_rom_11b_3005-23.c7", "f8c6cbe3688f256f41a121255fc08f575f6a4b4f"),
             ("2084_rom_12b_3005-24.e7", "fad7cea868ebf17347c4bc5193d647bbd8f9517b"))
_ROBOTRON_SOUND = ("video_sound_rom_3_std_767.ic12", "15afefef11bfc3ab78f61ab046701db78d160ec3")
_JOUST = (("joust_rom_1b_3006-13.e4", "0443e00ae2eb3e66cf805562ee04309487bb0ba4"),
          ("joust_rom_2b_3006-14.c4", "5fda266d43cbbf42eeae1a078b5209d9408ab99f"),
          ("joust_rom_3b_3006-15.a4", "686da120aa4bd4a41f3d93e8c79ebb343977851a"),
          ("joust_rom_4b_3006-16.e5", "cb1c3285344e2cfbe0a81ab9b51758c40da8a23f"),
          ("joust_rom_5b_3006-17.c5", "d9cac4c46820e1a451a145864bca7a35cfab7d37"),
          ("joust_rom_6b_3006-18.a5", "febaa8cf5c3a0af901cd12d0b7909f6fec3beadd"),
          ("joust_rom_7b_3006-19.e6", "5ad14aa65e71c3856dcdb04c99edda92e406a3e3"),
          ("joust_rom_8b_3006-20.c6", "f4ee13d5a95ed3e1050a3927a3a0ccf86ed7752d"),
          ("joust_rom_9b_3006-21.a6", "d95d3b746133eac9dcc9ee05eabecb797023f1a5"),
          ("joust_rom_10b_3006-22.a7", "90864a8ab944df45287bf0f68ad3a85194077a82"),
          ("joust_rom_11b_3006-23.c7", "6d38003d56bebeb1f5b4d2287d587342847aa195"),
          ("joust_rom_12b_3006-24.e7", "7d01764e8251c60b3cab96f7dc6dcc1c624f9d12"))
_JOUST_SOUND = ("video_sound_rom_4_std_780.ic12", "af7c066d2949d36b87ea8c425ca7d12f82b5c653")

_SINISTAR = (("sinistar_rom_1-b_16-3004-53.1d", "026d8cab07734fa294a5645edbe65a904bcbc302"),
             ("sinistar_rom_2-b_16-3004-54.1c", "423d1e3b0c07333ec582529bc4d0b7baf591820a"),
             ("sinistar_rom_3-b_16-3004-55.1a", "5bc03d7249529d827dc60c087e074ab3e4ea7361"),
             ("sinistar_rom_4-b_16-3004-56.2d", "72c0c3d5a5ca87ca4d95fcedaf834206e4633950"),
             ("sinistar_rom_5-b_16-3004-57.2c", "828a89d2ea13d8a362dae708f86bff54cb231887"),
             ("sinistar_rom_6-b_16-3004-58.2a", "703e29e6446856615760a4897c0f5d79cc7bdfb2"),
             ("sinistar_rom_7-b_16-3004-59.3d", "bf4e2ada6a59d246218544d822ba5355da925924"),
             ("sinistar_rom_8-b_16-3004-60.3c", "8c7eca656b2c23b0da41a8c7ce51a2735cab85a4"),
             ("sinistar_rom_9-b_16-3004-61.3a", "96e28e4fef98fff2649741a266fa590e0313e3b0"),
             ("sinistar_rom_10-b_16-3004-62.4c", "81802622bee8dbea5c0f08019d87d941dcdbe292"),
             ("sinistar_rom_11-b_16-3004-63.4a", "2f38e572ab9c731e38dfe9bad3cc8222a775c5ea"))
_SINISTAR_SOUND = (("3004_speech_ic7_r1_16-3004-52.ic7", "442f4f3ccd2e1db2136d2ffb121ea442921f87ca"),
                   ("3004_speech_ic5_r1_16-3004-50.ic5", "d5d51c550581c9d46ab331dd4fd32541a2ef598e"),
                   ("3004_speech_ic6_r1_16-3004-51.ic6", "16fa2a602acbbc182dd96bab113ab18356f3daf0"),
                   ("3004_speech_ic4_r1_16-3004-49.ic4", "44430cd5c110ec751b0bfb8ae99b26d443350db1"),
                   ("video_sound_rom_9_std.808.ic12", "c70c7dd6e88897920d7709a260f27810f66aade1"))


def _palette() -> list[int]:
    rg = ((1200, 560, 330), 0, 0)
    w, _ = resistor_weights(0, 255, -1.0, [rg, rg, ((560, 330), 0, 0)])
    out = []
    for i in range(256):
        r = combine(w[0], i & 1, (i >> 1) & 1, (i >> 2) & 1)
        g = combine(w[1], (i >> 3) & 1, (i >> 4) & 1, (i >> 5) & 1)
        b = combine(w[2], (i >> 6) & 1, (i >> 7) & 1)
        out.append(rgb565_swapped(r, g, b))
    return out


def _convert_palette(ctx: Context) -> None:
    with ctx.header("williams_palette.h") as f:
        emit_compressed(f, "williams_rgb", "unsigned short", "", 256, _palette(), plan=False)


def _rom(ctx: Context, name_sha1: tuple[str, str]) -> bytes:
    return bytes(ctx.rom(*name_sha1))


def _convert_defender(ctx: Context) -> None:
    main = (_rom(ctx, ("defend.1", "a57f5278ffe44248fc73f9925d107f4024ad981a"))
            + _rom(ctx, ("defend.4", "ed6ce796702ff32209ced3cb1ba3837dbafa526f"))
            + _rom(ctx, ("defend.2", "a9481478da38f99efb67f0ecf82d084e14b93b42"))
            + _rom(ctx, ("defend.3", "a24cfc55de56a72758c76fe2a55f1ec6c353b16f")))
    pages = {0x0000: ("defend.9", "67ccc194b1753a18af0c85f5e603355549c4f727"),
             0x0800: ("defend.12", "26e48dfeefa0766837b1e762695b9532dbc8bc5e"),
             0x1000: ("defend.8", "9d7bc3c89e5a53c575946f06702c722b864b1ff0"),
             0x1800: ("defend.11", "59b018ba0f3fe6eadfd387dc180ac281460358bc"),
             0x2000: ("defend.7", "2f89951dbe55d80df43df8dcf497171f73e726d3"),
             0x2800: ("defend.10", "9292b94b0d74e57e03aada4852ad1997c34122ff"),
             0x6000: ("defend.6", "a960fd1559ed74b81deba434391e49fc6ec389ca")}
    banked = bytearray(0x7000)  # MAME region: unloaded pages read 0
    for offs, name_sha1 in pages.items():
        banked[offs:offs + _PAGE] = _rom(ctx, name_sha1)
    sound = _rom(ctx, ("video_sound_rom_1.ic12", "ceb0d18483f0691978c604db94417e6941ad7ff2"))
    assert len(main) == 0x3000 and len(sound) == 0x800
    with ctx.header("defender_rom.h") as f:
        emit_compressed(f, "defender_rom_main", "unsigned char", "", len(main), list(main))
        emit_compressed(f, "defender_rom_banked", "unsigned char", "", len(banked), list(banked))
        emit_compressed(f, "defender_rom_sound", "unsigned char", "", len(sound), list(sound))


def _convert_two_cpus(ctx: Context, game: str, roms, snd) -> None:
    main = b"".join(_rom(ctx, r) for r in roms)  # 0x0000-0x8fff, then 0xd000-0xffff
    sound = _rom(ctx, snd)
    assert len(main) == 0xc000 and len(sound) == 0x1000
    with ctx.header(f"{game}_rom.h") as f:
        emit_compressed(f, f"{game}_rom_main", "unsigned char", "", len(main), list(main))
        emit_compressed(f, f"{game}_rom_sound", "unsigned char", "", len(sound), list(sound))


def _convert_sinistar(ctx: Context) -> None:
    main = b"".join(_rom(ctx, r) for r in _SINISTAR)  # 0x0000-0x8fff, then 0xe000-0xffff
    sound = b"".join(_rom(ctx, r) for r in _SINISTAR_SOUND)  # 0xb000-0xffff
    assert len(main) == 0xb000 and len(sound) == 0x5000
    with ctx.header("sinistar_rom.h") as f:
        emit_compressed(f, "sinistar_rom_main", "unsigned char", "", len(main), list(main))
        # PLAIN: in the Arena it breaks first-fit packing for some orders of
        # the board's three allocs; the 6808 reads it sparsely (speech bits)
        body = ",\n".join(", ".join(f"0x{v:02x}" for v in sound[i:i + 16])
                          for i in range(0, len(sound), 16))
        emit_plain(f, "sinistar_rom_sound", "unsigned char", "", len(sound), body)


def _job(game: str, convert) -> Job:
    return Job(name=game, convert=convert, machine=game, zips=(f"{game}.zip",), plan=True,
               deps=(_PALETTE_JOB,))


# the palette is the same for every game: one owner job, in the shared williams dir (never planned)
JOBS = (
    Job(name=_PALETTE_JOB, convert=_convert_palette, machine="williams"),
    _job("defender", _convert_defender),
    _job("robotron", partial(_convert_two_cpus, game="robotron", roms=_ROBOTRON, snd=_ROBOTRON_SOUND)),
    _job("joust", partial(_convert_two_cpus, game="joust", roms=_JOUST, snd=_JOUST_SOUND)),
    _job("sinistar", _convert_sinistar),
)
