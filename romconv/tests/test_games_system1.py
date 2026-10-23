from romconv.games.system1 import jobs
from romconv.games.system1.crypt import _CRYPT, _CRYPT2, _SWAPS2
from romconv.games.system1.sets import SETS


def test_every_game_depends_on_the_palette_owner():
    games = [j for j in jobs.JOBS if j.name != "system1_palette"]
    palette = next(j for j in jobs.JOBS if j.name == "system1_palette")
    assert len(games) == 15 and all(j.plan and j.deps == ("system1_palette",) for j in games)
    assert palette.machine == "system1" and not palette.plan


def test_pitfall2_reads_both_zips():
    job = next(j for j in jobs.JOBS if j.name == "pitfall2")
    assert job.zips == ("pitfall2.zip", "pitfall2u.zip")
    assert next(j for j in jobs.JOBS if j.name == "fourdwarrio").zips == ("4dwarrio.zip",)


def test_crypt_tables_are_complete():
    assert all(len(t) == 32 for t in _CRYPT.values())
    assert len(_SWAPS2) == 24
    for xor, swap in _CRYPT2.values():
        assert len(xor) == len(swap) == 128
    assert {s.crypt for s in SETS.values()} - {None} <= set(_CRYPT) | set(_CRYPT2) | {"mc8123"}


def test_decode_tiles_column_major_and_transposed():
    # one tile of 3 planes: plane 0 has only byte 0 (native row 0) = 0x80 (left pixel)
    rom = bytearray(24)
    rom[0] = 0x80
    cols, count = jobs._decode_tiles(bytes(rom))
    assert count == 1 and len(cols) == 8
    assert cols[0] == 0b100 << 21   # column 0, row 0 in bits 23..21, plane 0 = MSB
    rows, _ = jobs._decode_tiles(bytes(rom), transpose=True)
    assert rows[0] == 0b100 << 21   # row 0, column 0 first


def test_dedupe_tiles_first_use_order():
    words = [1] * 8 + [2] * 8 + [1] * 8
    unique, count, tile_map = jobs._dedupe_tiles(words)
    assert (count, tile_map) == (2, [0, 1, 0]) and unique == [1] * 8 + [2] * 8


def test_palette_endpoints():
    pal = jobs._palette565()
    assert len(pal) == 256 and pal[0] == 0 and pal[255] == 0xFFFF
    assert len(jobs._prom_palette565(bytes(768))) == 256


def test_plain_body_formats():
    assert jobs._plain_body("", [1, 2]) == "0x01, 0x02"
    assert jobs._plain_body("[2]", [1, 2, 3, 4]) == "{0x00000001, 0x00000002},\n{0x00000003, 0x00000004}"
