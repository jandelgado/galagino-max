import re
from pathlib import Path

import pytest

from romconv import registry
from romconv.core.context import Context
from romconv.core.paths import Paths, default
from romconv.emit.lzma import lzma_decompress
from romconv.gfx import color, logo
from romconv.gfx.bits import bit_permute_step, decode_data

ROMCONV = Path(__file__).resolve().parents[1]


# gfx.color: one known value of each formula


def test_rgb565_swapped_scaled_matches_namco_hw():
    assert color.rgb565_swapped_scaled(255, 128, 7) == 0xE0FB


def test_rgb565_swapped_matches_gfxutil_and_clamps():
    assert color.rgb565_swapped(255, 128, 7) == 0x00FC
    assert color.rgb565_swapped(300, -5, 7) == 0x00F8


def test_rgb565_be_matches_mrdo():
    assert color.rgb565_be(255, 128, 7) == 0xFC00
    assert color.swap_bytes(0xFC00) == 0x00FC


def test_rgb565_swapped_packed_matches_namco_hw():
    assert color.rgb565_swapped_packed(0x5A) == 0x6A43


def test_rgb565_swapped_4bit_matches_cmapconv():
    assert color.rgb565_swapped_4bit(9, 5, 3) == 0x8692


def test_rgb565_swapped_dkong_matches_cmapconv():
    assert color.rgb565_swapped_dkong(0x6D) == 0x9594


# generic converters through real game jobs, against the golden baseline snapshot

_BASE = ROMCONV / "build" / "golden" / "base" / "source" / "src" / "machines"


def _ctx(tmp_path, log, job):
    """Context on a temp repo that links the real romszip and samples; skips without them."""
    real = default().romszip
    for z in job.zips:
        if not (real / z).is_file():
            pytest.skip(f"romszip/{z} missing")
    if not _BASE.is_dir():
        pytest.skip("no golden baseline (dev.golden run base)")
    (tmp_path / "romszip").symlink_to(real)
    if default().samples.is_dir():
        (tmp_path / "samples").symlink_to(default().samples)
    return Context(job, Paths(tmp_path), (), log)


@pytest.mark.slow
@pytest.mark.parametrize("name", ["pacman", "eyes", "frogger", "dkong", "galaga"])
def test_game_job_matches_baseline(tmp_path, log, name):
    # pacman: charmap, spritemap, colormap, wavetable, compressed rom; eyes: data bit swap;
    # frogger: charmap_frogger; dkong: colormap_dkong, samples; galaga: patched rom
    job = registry.by_name(name)
    ctx = _ctx(tmp_path, log, job)
    job.convert(ctx)
    made = sorted(p for p in ctx.paths.machines.rglob("*.h"))
    assert made
    for path in made:
        want = _BASE / path.relative_to(ctx.paths.machines)
        assert path.read_text() == want.read_text(), path.name


# logo: the legacy tool is gone, so check the stream and a real logo against the baseline snapshot


def _png(path, colors):
    from PIL import Image

    img = Image.new("RGB", (logo.WIDTH, logo.HEIGHT))
    img.putdata([colors[(x // 8 + y // 8) % len(colors)] for y in range(logo.HEIGHT) for x in range(logo.WIDTH)])
    img.save(path)
    return img


def test_logo_stream_reproduces_image(tmp_path, log):
    colors = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (255, 255, 255)]
    png = tmp_path / "logos" / "foo.png"
    png.parent.mkdir()
    img = _png(png, colors)
    paths = Paths(tmp_path)
    (paths.machines / "foo").mkdir(parents=True)
    ctx = Context(logo.LOGOS_JOB, paths, (), log)
    logo.logo(ctx, png, "foo")
    text = (paths.machines / "foo" / "foo_logo.h").read_text()
    assert text.startswith('#include "../../emulation/logo.h"\n')
    assert "static constexpr Logo foo_logo(foo_logo_blob);" in text
    assert "ASSETS_REMOTE" not in text
    packed = bytes(int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{2}", text.split("foo_logo_packed[]")[1].split("};")[0]))
    raw = lzma_decompress(packed)
    assert len(raw) == 32 + logo.WIDTH * logo.HEIGHT // 2
    palette = [int.from_bytes(raw[2 * i:2 * i + 2], "little") for i in range(16)]
    pixels = []
    for b in raw[32:]:
        pixels += [b >> 4, b & 15]
    want = [color.rgb565_swapped_scaled(*c) for c in img.get_flattened_data()]
    assert [palette[i] for i in pixels] == want


def test_logo_job_names_and_skips_missing_machines(tmp_path, log):
    paths = Paths(tmp_path)
    (paths.logos).mkdir()
    for stem in ("invaders", "milliped", "nomachine"):
        _png(paths.logos / f"{stem}.png", [(0, 0, 0), (255, 255, 255)])
    for d in ("spaceinvaders", "millipede"):
        (paths.machines / d).mkdir(parents=True)
    ctx = Context(logo.LOGOS_JOB, paths, (), log)
    logo.LOGOS_JOB.convert(ctx)
    made = sorted(p.relative_to(paths.machines).as_posix() for p in paths.machines.rglob("*.h"))
    assert made == ["millipede/millipede_logo.h", "spaceinvaders/spaceinvaders_logo.h"]


@pytest.mark.slow
def test_logo_matches_baseline_snapshot(tmp_path, log):
    base = ROMCONV / "build" / "golden" / "base" / "source" / "src" / "machines" / "pacman" / "pacman_logo.h"
    png = default().logos / "pacman.png"
    if not (base.is_file() and png.is_file()):
        pytest.skip("no baseline snapshot of the pacman logo")
    paths = Paths(tmp_path)
    ctx = Context(logo.LOGOS_JOB, paths, (), log)
    logo.logo(ctx, png, "pacman")
    assert (paths.machines / "pacman" / "pacman_logo.h").read_text() == base.read_text()


# bits


def test_decode_data_swaps_bits_of_each_block():
    data = bytes(range(16))
    want = []
    for block in (0, 8):
        swapped = [data[block + bit_permute_step(j, 1, 2)] for j in range(8)]
        want += [bit_permute_step(v, 16, 2) for v in swapped]
    assert decode_data(data) == want
