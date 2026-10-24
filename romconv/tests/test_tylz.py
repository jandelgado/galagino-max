from romconv import registry
from romconv.games import tylz


def test_job_registered():
    assert tylz.JOB.machine == "tylz" and tylz.JOB.zips == ("tylz.zip", "votrax-qbert.zip")
    assert {j.name: j for j in registry.ALL}["tylz"] is tylz.JOB


def test_tile_rows_lowest_nibble_first():
    bg = bytearray(0x2000)
    bg[0:4] = bytes([0x01, 0x23, 0x45, 0x67])  # tile 0, row 0: pen = x (packed msb)
    words = tylz._tile_words(bytes(bg))
    assert len(words) == 256 * 8
    assert words[0] == 0x76543210 and sum(words) == words[0]


def test_sprite_rows_two_words():
    spr = [[[0] * 16 for _ in range(16)]]
    spr[0][3] = list(range(16))  # row 3: pen = x
    words = tylz._sprite_words(spr)
    assert len(words) == 32
    assert words[6] == 0x76543210 and words[7] == 0xfedcba98
    assert sum(words) == words[6] + words[7]
