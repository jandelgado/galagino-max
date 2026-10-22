import hashlib
import zipfile

import pytest

from romconv import registry
from romconv.core.paths import default
from romconv.games import contra

_ZIP = default().romszip / "contra.zip"


def test_job_registered():
    assert contra.JOB.machine == "contra" and contra.JOB.zips == ("contra.zip",)
    assert {j.name: j for j in registry.ALL}["contra"] is contra.JOB


def test_interleave_even_odd():
    assert contra.interleave(b"\x01\x03", b"\x02\x04") == b"\x01\x02\x03\x04"


def test_decode_packed_msb_high_nibble_is_left_pixel():
    tile = contra.decode_tiles(bytes([0x12]) + bytes(31))[0]
    assert tile[0][0] == 1 and tile[0][1] == 2 and tile[1][0] == 0


def test_rotate_native_x_is_panel_line_and_y_counts_down_columns():
    tile = [[0] * 8 for _ in range(8)]
    tile[1][3] = 5  # native y 1, x 3
    panel = contra.rotate(tile)
    assert panel[3][6] == 5 and sum(map(sum, panel)) == 5


def test_pack_column_c_in_nibble_c():
    line = [1, 2, 3, 4, 5, 6, 7, 8]
    assert contra.pack([line] + [[0] * 8] * 7)[0] == 0x87654321


def test_dedupe_index_reproduces_every_tile():
    tiles = [(1,) * 8, (2,) * 8, (1,) * 8, (3,) * 8]
    unique, index = contra.dedupe(tiles)
    assert unique == [(1,) * 8, (2,) * 8, (3,) * 8]
    assert [unique[i] for i in index] == tiles


def test_real_gfx_unique_count():
    if not _ZIP.is_file():
        pytest.skip("romszip/contra.zip missing")
    with zipfile.ZipFile(_ZIP) as z:
        words = contra.gfx_words(z.read)
    unique, index = contra.dedupe(words)
    assert len(words) == 2 * 16384 and len(unique) == 17138
    assert all(unique[index[i]] == words[i] for i in range(0, len(words), 97))
