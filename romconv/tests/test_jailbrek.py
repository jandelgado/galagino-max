import zipfile

import pytest

from romconv import registry
from romconv.core.errors import ConversionError
from romconv.core.paths import default
from romconv.games import jailbrek
from romconv.gfx.color import rgb565_swapped, swap_bytes

_ZIP = default().romszip / "jailbrek.zip"

# MAME 0.289 jailbrek palette (indirect colors 0-31, 0xAARRGGBB), dumped via Lua
# screen.palette:indirect_color(i)
_MAME = (
    0xff000000, 0xffdd0000, 0xffffdd22, 0xff660000, 0xff779977, 0xff001100, 0xff772200, 0xffaa6600,
    0xffee9966, 0xff000044, 0xff003399, 0xff0099dd, 0xff555555, 0xff999999, 0xffcccccc, 0xff110011,
    0xff000000, 0xff990000, 0xffcc0000, 0xff009900, 0xff00cc00, 0xff555555, 0xffaaaaaa, 0xff0000ff,
    0xff66aadd, 0xff995522, 0xffcc8844, 0xffffff00, 0xff006600, 0xff888888, 0xff0022aa, 0xffffffff,
)


def test_job_registered():
    job = jailbrek.JOB
    assert job.name == "jailbrek" and job.machine == "jailbrek" and job.plan
    assert job.zips == ("jailbrek.zip",)
    assert {j.name: j for j in registry.ALL}["jailbrek"] is job


def _member(z, entry):
    return z.read(entry[0])


def test_jailbrek_palette_matches_mame():
    if not _ZIP.is_file():
        pytest.skip("romszip/jailbrek.zip missing")
    with zipfile.ZipFile(_ZIP) as z:
        pal = jailbrek._palette(_member(z, jailbrek._PAL_RG), _member(z, jailbrek._PAL_B))
    assert len(pal) == 32
    expect = [rgb565_swapped((c >> 16) & 255, (c >> 8) & 255, c & 255) for c in _MAME]
    assert pal == expect
    assert swap_bytes(pal[2]) == 0xfee4  # ffdd22


def test_palette_is_pal4bit():
    pal = jailbrek._palette(bytes([0xf1] + [0] * 31), bytes([0x0a] + [0] * 31))
    assert pal[0] == rgb565_swapped(0x11, 0xff, 0xaa)


def test_vlm_halves_must_match():
    half = bytes(range(256)) * 32
    assert jailbrek._vlm_rom(half + half) == half
    with pytest.raises(ConversionError, match="halves differ"):
        jailbrek._vlm_rom(half + bytes(8192))


def test_real_sets_have_expected_sizes():
    if not _ZIP.is_file():
        pytest.skip("romszip/jailbrek.zip missing")
    with zipfile.ZipFile(_ZIP) as z:
        assert sum(len(_member(z, e)) for e in jailbrek._MAIN) == 32768
        assert sum(len(_member(z, e)) for e in jailbrek._TILES) == 1024 * 32
        assert sum(len(_member(z, e)) for e in jailbrek._SPRITES) == 512 * 128
        assert len(jailbrek._vlm_rom(_member(z, jailbrek._VLM))) == 8192
