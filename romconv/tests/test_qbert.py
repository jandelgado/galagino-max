import zipfile

import pytest

from romconv import registry
from romconv.core.paths import default
from romconv.games import qbert

_ZIP = default().romszip / "qbert.zip"


def test_job_registered():
    assert qbert.JOB.machine == "qbert" and qbert.JOB.zips == ("qbert.zip",)
    assert {j.name: j for j in registry.ALL}["qbert"] is qbert.JOB


def test_tile_word_per_native_x_nibble_per_native_y():
    bg = bytearray(0x2000)
    bg[2 * 4 + 2] = 0x07  # tile 0, row 2, x 5 (packed msb: odd x = low nibble)
    words = qbert.tile_words(bytes(bg))
    assert len(words) == 256 * 8
    assert words[5] == 7 << 8 and sum(words) == words[5]


def test_sprite_fg3_is_msb_plane():
    fg = bytearray(0x8000)
    fg[0] = 0x80           # plane 0 (qb-fg3), sprite 0, y 0, x 0
    fg[0x6000 + 2] = 0x10  # plane 3 (qb-fg0), sprite 0, y 1, x 3
    spr = qbert.decode_sprites(bytes(fg))
    assert spr[0][0][0] == 8 and spr[0][1][3] == 1


def test_sprite_words_two_per_native_x():
    spr = [[[0] * 16 for _ in range(16)]]
    spr[0][9][3] = 5  # y 9, x 3
    words = qbert.sprite_words(spr)
    assert len(words) == 32 and words[3 * 2 + 1] == 5 << 4 and sum(words) == words[7]


def test_real_main_image_reset_vector():
    if not _ZIP.is_file():
        pytest.skip("romszip/qbert.zip missing")
    with zipfile.ZipFile(_ZIP) as z:
        img = qbert.main_image(z.read)
    assert len(img) == 0x6000
    assert img[0x5FF0:0x5FF5] == bytes([0xEA, 0x00, 0xA0, 0x00, 0x00])


def test_levels_from_resistor_weights():
    lv = qbert.levels()
    assert len(lv) == 16 and lv[0] == 0 and lv[15] == 255
    assert lv == sorted(lv)
