import pytest

from romconv import registry
from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.games import (hero, ladybug, llander, mario, megazone, mhavoc, mikie, millipede, missile, motorace,
                           mpatrol, mspacman, nibbler, pbaction, phoenix, pleiads, pooyan)
from romconv.games.mario.palette import rgb_palette
from romconv.gfx.color import rgb565_swapped
from romconv.hw import snk6502

JOBS = (hero.JOB, ladybug.JOB, llander.JOB, mario.JOB, megazone.JOB, mhavoc.JOB, mikie.JOB, millipede.JOB,
        missile.JOB, motorace.JOB, mpatrol.JOB, mspacman.JOB, nibbler.JOB, pbaction.JOB, phoenix.JOB,
        pleiads.JOB, pooyan.JOB)


def _ctx(job, paths, log):
    return Context(job, paths, (), log)


def test_job_shapes():
    jobs = {j.name: j for j in JOBS}
    assert len(jobs) == 17
    assert not any(j.plan for j in jobs.values())
    assert jobs["mspacman"].deps == ("pacman",) and jobs["mspacman"].zips == ("mspacman.zip",)
    assert jobs["millipede"].zips == ("milliped.zip",) and jobs["nibbler"].zips == ("nibblerp.zip",)
    assert jobs["hero"].zips == ("H.E.R.O..zip",)
    assert all(j.machine == j.name for j in jobs.values())


def test_registered():
    registered = {j.name: j for j in registry.ALL}
    for job in JOBS:
        assert registered[job.name] is job
    assert "pacman" in {j.name for j in registry.resolve(["mspacman"])}


@pytest.mark.parametrize("job", [j for j in JOBS if j is not hero.JOB], ids=lambda j: j.name)
def test_missing_zip_is_a_conversion_error_naming_the_zip(job, paths, log):
    with pytest.raises(ConversionError, match=job.zips[0].replace(".", r"\.")):
        job.convert(_ctx(job, paths, log))


def test_missing_member_names_zip_and_file(paths, log, make_zip):
    make_zip("llander.zip", {"other": b""})
    with pytest.raises(ConversionError, match=r"llander\.zip.*034572-02\.f1"):
        llander.JOB.convert(_ctx(llander.JOB, paths, log))


def test_hero_needs_a_cart(paths, log):
    # no H.E.R.O..zip: the loose PAL cart is the fallback, and it is missing too
    with pytest.raises(ConversionError, match=r"H\.E\.R\.O\.-pal\.bin"):
        hero.JOB.convert(_ctx(hero.JOB, paths, log))


def test_hero_palettes():
    assert len(hero._STELLA_PAL) == 128 and len(hero._NTSC_IQ) == 16
    assert hero._ntsc_rgb(0) == [0, 0, 0]
    assert all(0 <= c <= 255 for i in range(128) for c in hero._ntsc_rgb(i))
    # PAL hue 1 is NTSC hue 1 mapped to PAL 2, luminance kept
    assert hero._pal_rgb((1 << 3) | 5) == [hero._STELLA_PAL[(2 << 3) | 5] >> 16,
                                           (hero._STELLA_PAL[(2 << 3) | 5] >> 8) & 255,
                                           hero._STELLA_PAL[(2 << 3) | 5] & 255]
    assert hero._palette("p", hero._ntsc_rgb).startswith("static const uint16_t p[128] = {\n")


def test_ladybug_gfx_decoders():
    data = bytearray(0x2000)
    data[0], data[0x1000] = 0x01, 0x01  # x 0 y 0 in both planes -> pen 3
    words = ladybug._decode_tiles(data)
    assert len(words) == 512 * 8 and words[0] == 3 and words[1] == 0
    assert len(ladybug._decode_sprites(bytes(0x2000))) == 128 * 16


def test_ladybug_colormaps():
    # inverted PROM outputs: 0xff is black, 0x00 white
    assert ladybug._rgb_palette(bytes([0xFF, 0x00])) == [0x0000, 0xFFFF]
    chars, sprites = ladybug._colormaps(bytes(32), bytes(32))
    assert len(chars) == len(sprites) == 8
    assert all(pens == [0, 0xFFFF, 0xFFFF, 0xFFFF] for pens in chars + sprites)


def test_mario_data_files():
    # inverted PROM: 0xff is black; MAME's palette for 00 is near white
    pal = rgb_palette(bytes(range(256)))
    assert len(pal) == 256 and pal[0xff] == (0, 0, 0) and pal[0x00] == (254, 252, 255)
    pcms = mario.samples()
    assert list(pcms) == ["walk0", "walk1", "skid"]
    for pcm in pcms.values():
        assert len(pcm) > 100 and pcm.dtype.name == "int8"
    # walk ends in the thump of the one-shot falling, close to full scale
    assert pcms["walk0"].min() < -90 and pcms["skid"].min() > -40


def test_motorace_bitswap_and_decrypt():
    assert motorace._bitswap8(0b00000001, 0, 1, 2, 3, 4, 5, 6, 7) == 0b10000000
    assert motorace._bitswap16(1, 0, *range(1, 16)) == 0x8000
    out = motorace._decrypt_motorace(bytes(range(256)) * 32)
    assert len(out) == 0x2000 and sorted(out) != list(out)
    assert motorace._pack_tile_row([1, 2, 3, 4, 5, 6, 7, 0]) == 0x12345670
    assert motorace._pack_sprite_row(list(range(8)) * 2) == (0x01234567, 0x01234567)


def test_motorace_prom_colors():
    assert motorace._prom_to_rgb(0) == (0, 0, 0)
    assert motorace._prom_to_rgb(0xff) == (0xff, 0xff, 0xff)


def test_mpatrol_decoding_helpers():
    assert len(mpatrol._decode_chars(bytes(0x2000))) == 512 * 8
    assert len(mpatrol._decode_sprites(bytes(0x4000))) == 128 * 16
    # background decode needs the 0xff fill to be solid pen 3 below row 64
    assert len(mpatrol._decode_bg(bytes(0x1000))) == 64 * 64
    pals = mpatrol._palettes([0] * 512, [0xff] * 32, [0] * 32)
    assert pals[0][0] == (0, 0, 0) and pals[1][0] == (255, 255, 255)


def test_mspacman_address_and_data_swaps():
    for i in range(0x800):
        assert 0 <= mspacman._addr_swap11(i) < 0x800
        assert 0 <= mspacman._addr_swap12(i) < 0x1000
    assert sorted(mspacman._decode_byte(b) for b in range(256)) == list(range(256))
    assert mspacman._decode_byte(1) == 0x80


def test_pbaction_sprite32_check():
    quad = [[1] * 16 for _ in range(16)]
    s16 = [quad] * 256
    s32 = [[[1] * 32 for _ in range(32)] for _ in range(32)]
    pbaction._check_sprites32(s16, s32)
    s32[0][0][0] = 2
    with pytest.raises(AssertionError):
        pbaction._check_sprites32(s16, s32)


def test_phoenix_palette_and_tiles():
    palette = phoenix._decode_palette(bytes(128), bytes(128))
    assert palette == [rgb565_swapped(0, 0, 0)] * 256
    out = phoenix._decode_tile_pens(bytes([0x01] + [0] * 2047), bytes([0x01] + [0] * 2047))
    assert len(out) == 0x4000 and out[0] == 3


def test_pleiads_sim_patch_and_palette():
    # MAME pens: 0 black, 7/7 white, 1/1 (pure red) ca1618
    prom = bytes([0, 1] + [7] * 254)
    palette = pleiads._palette(prom, prom)
    assert palette[0] == rgb565_swapped(0, 0, 0)
    assert palette[0x04] == rgb565_swapped(0xca, 0x16, 0x18)
    assert palette[0xff] == rgb565_swapped(255, 255, 255)


def test_pooyan_palette_and_pixels():
    pal = pooyan._convert_palette(bytes([0, 0xff] + [0] * 30))
    assert pal[0] == 0 and pal[1] == rgb565_swapped(255, 255, 255)
    assert pooyan._pixel4(bytes([0x80]), bytes([0]), 0, 0) == 1
    assert pooyan._pixel4(bytes([0]), bytes([0x80]), 0, 0) == 4
    char_colors, sprite_colors, transmask = pooyan._build_colormap(list(range(32)), bytes(256), bytes(256))
    assert len(char_colors) == len(sprite_colors) == 256 and transmask == [0xffff] * 16


def test_nibbler_crc_manifest_rejects_a_bad_dump(paths, log, make_zip):
    make_zip("nibblerp.zip", {"ic12": b"\x00" * 0x1000})
    ctx = _ctx(nibbler.JOB, paths, log)
    with pytest.raises(ConversionError, match="CRC32"):
        snk6502.load(ctx, nibbler._MANIFEST, "ic12")
    with pytest.raises(ConversionError, match="no file ic7"):
        snk6502.load(ctx, nibbler._MANIFEST, "ic7")


def test_mrgoemon_job():
    from romconv.games import mrgoemon
    job = mrgoemon.JOB
    assert job.name == "mrgoemon" and job.machine == "mrgoemon" and job.plan
    assert job.zips == ("mrgoemon.zip",)
    assert job in registry.ALL
