from romconv.core.context import SHARED_DIRS
from romconv.games import mcr, williams


def test_williams_games_depend_on_the_palette_owner():
    owner, *games = williams.JOBS
    assert owner.name == "williams_palette" and owner.machine == "williams" and not owner.plan
    assert "williams" in SHARED_DIRS
    assert [g.name for g in games] == ["defender", "robotron", "joust", "sinistar"]
    assert all(g.plan and g.deps == ("williams_palette",) for g in games)


def test_williams_palette_endpoints():
    pal = williams._palette()
    assert len(pal) == 256 and pal[0] == 0 and pal[255] == 0xFFFF


def test_mcr_jobs_read_the_ssio_prom_zip():
    assert [j.name for j in mcr.JOBS] == ["tapper", "tron", "shollow", "dotron"]
    assert all("midssio.zip" in j.zips for j in mcr.JOBS)
    assert [j.plan for j in mcr.JOBS] == [True, False, False, False]


def test_pack_row_first_pixel_in_top_nibble():
    assert mcr._pack_row([1, 2, 3, 4, 5, 6, 7, 8]) == 0x12345678


def test_rotate_is_a_quarter_turn():
    # panel[row][col] = native[size - 1 - col][row]
    assert mcr._rotate([[1, 2], [3, 4]]) == [[3, 1], [4, 2]]


def test_downsample_picks_one_pixel_per_block():
    sprite = [[(x + 100 * y) for x in range(32)] for y in range(32)]
    out = mcr._downsample(sprite, 1, 0)
    assert len(out) == 16 and len(out[0]) == 16
    assert out[3][5] == sprite[6][11]
