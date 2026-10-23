import zipfile

import pytest

from romconv import registry
from romconv.core.paths import default
from romconv.games import gberet, scotrsht
from romconv.gfx.color import rgb565_swapped

_ZIP = default().romszip / "scotrsht.zip"


def _member(z, entry):
    return z.read(entry[0])


def test_scotrsht_job():
    job = scotrsht.JOB
    assert job.name == "scotrsht" and job.machine == "scotrsht" and job.plan
    assert job.zips == ("scotrsht.zip",)
    assert {j.name: j for j in registry.ALL}["scotrsht"] is job


def test_scotrsht_rom_layout():
    g03 = bytes(i * 7 & 255 for i in range(0x8000))
    g02 = bytes(0xc0 + (i & 15) for i in range(0x4000))
    img = scotrsht.main_image(g03, g02)
    assert len(img) == 0xc000

    def cpu(a):
        return img[a - 0x4000]

    assert cpu(0x8000) == g03[0]
    assert cpu(0x4000) == g03[0x4000]
    assert cpu(0xbfff) == g03[0x3fff]
    assert cpu(0x7fff) == g03[0x7fff]
    assert cpu(0xc000) == g02[0]


def test_palette_is_pal4bit_rgb_proms():
    pad = bytes(254)
    r, g, b = bytes([0x0f, 1]) + pad, bytes([0x05, 2]) + pad, bytes([0x0a, 3]) + pad
    pal = scotrsht.palette(r, g, b)
    assert pal[0] == rgb565_swapped(0xff, 0x55, 0xaa)
    assert pal[1] == rgb565_swapped(0x11, 0x22, 0x33)


def test_lut_low_nibble():
    assert scotrsht.lut(bytes([0xf3, 0x1a])) == bytes([3, 10])


def test_tile_rotated_nibble_lsb_first():
    # high nibble = left pixel; native (x=1, y=0) = 5 -> panel line 1, column 7
    data = bytes([0x05]) + bytes(31)
    t = scotrsht.tiles(data, 1)
    assert len(t) == 32
    line1 = t[4:8]
    assert line1 == [0, 0, 0, 0x50]
    assert sum(t) == 0x50


def test_sprite_layout_matches_gberet():
    data = bytes((i * 13 + 5) & 255 for i in range(128))
    flat = scotrsht.sprites(data, 1)
    assert len(flat) == 128
    g = gberet.decode_sprites(data, 1)  # [row][0|1] words, first pixel in bits 31-28
    for row in range(16):
        for col in range(16):
            word = g[row * 2 + col // 8]
            px = (word >> (28 - 4 * (col % 8))) & 15
            assert (flat[row * 8 + col // 2] >> (4 * (col & 1))) & 15 == px


def test_real_set():
    if not _ZIP.is_file():
        pytest.skip("romszip/scotrsht.zip missing")
    with zipfile.ZipFile(_ZIP) as z:
        assert len(_member(z, scotrsht._G03)) == 0x8000
        assert len(_member(z, scotrsht._G02)) == 0x4000
        assert len(_member(z, scotrsht._SOUND)) == 0x4000
        assert len(_member(z, scotrsht._TILES)) == 1024 * 32
        assert sum(len(_member(z, e)) for e in scotrsht._SPRITES) == 512 * 128
        pal = scotrsht.palette(*(_member(z, e) for e in scotrsht._PROMS))
    assert len(pal) == 256
