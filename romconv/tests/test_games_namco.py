"""Offline checks of the Mappy and Tower of Druaga C++ logic (formerly
internal/mappy/test_mappy_logic.py and internal/todruaga/test_todruaga_logic.py), plus
the todruaga conversion output."""
import re

import pytest

from romconv.core.context import Context
from romconv.core.paths import Paths, default
from romconv.games.namco import todruaga


def _next(n):
    return ((n ^ 0x90) if (n & 1) else n) >> 1


def _customio_mode5(ram):
    """namcoio 58XX mode 5 (bootup check LFSR): exact replica of mappy.cpp customio_run case 5."""
    out = [0] * 8
    n = ((ram[9] & 0x0F) * 16 + (ram[10] & 0x0F)) & 0x7F
    seed = 0x22
    for _ in range(n):
        seed = _next(seed)
    for i in range(1, 8):
        n = 0
        rng = seed
        if rng & 1:
            n ^= ~(ram[11] & 0x0F)
        rng = _next(rng)
        seed = rng
        for reg in (10, 9, 15, 14, 13, 12):
            if rng & 1:
                n ^= ~(ram[reg] & 0x0F)
            rng = _next(rng)
        out[i] = (~n) & 0x0F
    out[0] = 0x0F if (ram[9] & 0x0F) == 0x0F else 0x00
    return out


def _customio56_mode8(ram):
    """namcoio 56XX mode 8 (bootup check by checksum): ram[0] = sum >> 4, ram[1] = sum & 0xF."""
    s = sum(ram[i] & 0x0F for i in range(9, 16))
    return [(s >> 4) & 0x0F, s & 0x0F]


def _mame_scan(col, row):
    """TILEMAP_MAPPER mappy_tilemap_scan (col 0..35, row 0..59)."""
    col -= 2
    if col & 0x20:
        if row & 0x20:
            return 0x7FF
        return ((row + 2) & 0x0F) + (row & 0x10) + ((col & 3) << 5) + 0x780
    return col + (row << 5)


def _my_indices(row, scroll):
    """The tile indices render_tiles touches; strip r = hardware column."""
    colp = row - 2
    out = {}
    if row < 2 or row > 33:
        for rt in range(28):
            out[rt] = ((rt + 2) & 0x0F) + (rt & 0x10) + ((colp & 3) << 5) + 0x780
        return out
    rt0 = scroll >> 3
    for k in range(29):
        rt = (rt0 + k) % 60
        out[rt] = (rt << 5) + colp
    return out


def test_mode5_bootup_check_mappy():
    # mappy writes 9-15 = 3 6 5 f a c e e, expects ram[1..7] = 8 4 6 e d 9 d (else no start)
    ram = [0] * 16
    ram[9:16] = [3, 6, 5, 0xF, 0xA, 0xC, 0xE]
    assert _customio_mode5(ram) == [0x0, 0x8, 0x4, 0x6, 0xE, 0xD, 0x9, 0xD]


def test_mode5_bootup_check_grobda():
    ram = [0] * 16
    ram[9:16] = [2, 3, 4, 5, 6, 7, 8]
    got = _customio_mode5(ram)
    assert got[2] == 0xF and got[6] == 0xC


def test_mode8_checksum_superpac_motos_phozon():
    ram = [0] * 16
    ram[9:16] = [0xF] * 7  # superpac/motos expect 6 9
    assert _customio56_mode8(ram) == [0x6, 0x9]
    ram[9:16] = [1, 2, 3, 4, 5, 6, 7]  # phozon expects 1 c
    assert _customio56_mode8(ram) == [0x1, 0xC]


def test_fixed_columns_match_mame_scan():
    for row in (0, 1, 34, 35):
        mine = _my_indices(row, 0)
        for rt in range(28):
            assert mine[rt] == _mame_scan(row, rt), (row, rt)


def test_scrolling_columns_match_mame_scan():
    for row in range(2, 34):
        for scroll in range(0, 256, 17):
            for rt, idx in _my_indices(row, scroll).items():
                assert idx == _mame_scan(row, rt), (row, scroll, rt)


def test_scroll_covers_every_pixel_once():
    # x_block = 216 - 8k + fine, tiles 8 wide: pixels 0..223 covered exactly once
    for scroll in range(256):
        fine = scroll & 7
        cover = [0] * 224
        for k in range(29):
            x = 216 - 8 * k + fine
            for c in range(8):
                if 0 <= x + c < 224:
                    cover[x + c] += 1
        assert all(c == 1 for c in cover), f"scroll {scroll}"


def test_scroll_pixel_maps_to_hardware_row():
    # tile rt on screen shows hardware row mameY = screenY + scroll (mod 480), gal_x = 223 - screenY
    for scroll in range(0, 256, 31):
        fine = scroll & 7
        rt0 = scroll >> 3
        for k in range(29):
            x_block = 216 - 8 * k + fine
            rt = (rt0 + k) % 60
            for c in range(8):
                gal_x = x_block + c
                if not 0 <= gal_x < 224:
                    continue
                mame_y = (223 - gal_x + scroll) % 480
                assert mame_y // 8 == rt, (scroll, k, c)
                assert mame_y % 8 == 7 - c, (scroll, k, c)


def test_hiscore_regions_inside_work_ram():
    for base, length in ((0x102A, 0x32), (0x100B, 0x03)):
        assert base + length <= 0x2800


# todruaga against the real ROM set / the generated headers


def _ctx(tmp_path, log):
    real = default().romszip
    if not (real / "todruaga.zip").is_file():
        pytest.skip("romszip/todruaga.zip missing")
    (tmp_path / "romszip").symlink_to(real)
    return Context(todruaga.JOB, Paths(tmp_path), (), log)


def _vector(rom, base, offset):
    return (rom[offset - base] << 8) | rom[offset - base + 1]


def test_todruaga_rom_vectors(tmp_path, log):
    # RESET/IRQ vectors of both 6809 ROMs point into their windows (td2_3.1d@0x8000 + td2_1.1b@0xC000)
    ctx = _ctx(tmp_path, log)
    main = ctx.rom("td2_3.1d") + ctx.rom("td2_1.1b")
    sub = ctx.rom("td1_4.1k")
    assert 0x8000 <= _vector(main, 0x8000, 0xFFFE) <= 0xFFFF
    assert 0x8000 <= _vector(main, 0x8000, 0xFFF8) <= 0xFFFF
    assert 0xE000 <= _vector(sub, 0xE000, 0xFFFE) <= 0xFFFF
    assert 0xE000 <= _vector(sub, 0xE000, 0xFFF8) <= 0xFFFF


def _rows(text, symbol):
    body = re.search(re.escape(symbol) + r"\[\]\[\d+\] = \{(.*?)\n\};", text, re.S).group(1)
    return body.count("{")


@pytest.mark.slow
def test_todruaga_output_sizes(tmp_path, log):
    ctx = _ctx(tmp_path, log)
    todruaga.JOB.convert(ctx)
    cmap = (ctx.paths.machines / "todruaga" / "todruaga_cmap.h").read_text()
    assert _rows(cmap, "const unsigned short todruaga_colormap_tiles") == 64
    assert _rows(cmap, "const unsigned short todruaga_colormap_tiles_prio") == 64
    assets = {a.name: a for a in ctx.manifest.assets()}
    assert assets["todruaga_colormap_sprites"].count == 64
    assert len(assets["todruaga_rom_main"].raw) == 0x8000
    assert len(assets["todruaga_rom_sub"].raw) == 0x2000
