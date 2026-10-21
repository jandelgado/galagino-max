import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.core.paths import Paths, default
from romconv.emit.lzma import lzma_decompress
from romconv.gfx import cmap, color, logo, sample, sprites, tiles, wavetable
from romconv.gfx.bits import bit_permute_step, decode_data
from romconv.gfx.rom import emit_rom

ROMCONV = Path(__file__).resolve().parents[1]


def _noop(ctx):
    pass


# gfx.color: one known value of each legacy formula


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


# generic converters against the legacy tools (removed with them in Task 16)


def _ctx(tmp_path, log, *zips):
    """Context on a temp repo that links the real romszip; skips without the zips."""
    real = default().romszip
    for z in zips:
        if not (real / z).is_file():
            pytest.skip(f"romszip/{z} missing")
    (tmp_path / "romszip").symlink_to(real)
    if default().samples.is_dir():
        (tmp_path / "samples").symlink_to(default().samples)
    return Context(Job("foo", _noop, machine="foo", zips=zips), Paths(tmp_path), (), log)


def _legacy(tmp_path, ctx, tool, args, roms=()):
    """Run internal/<tool> on the ROMs extracted from the job's zips; "@name"
    in args is the path of ROM file name. Returns the legacy output text."""
    rom_dir = tmp_path / "roms"
    rom_dir.mkdir(exist_ok=True)
    for name in roms:
        (rom_dir / name).write_bytes(ctx.rom(name))
    run = len(list((tmp_path / "legacy").glob("*"))) if (tmp_path / "legacy").is_dir() else 0
    out = tmp_path / "legacy" / str(run) / "machines" / "foo" / "x.h"
    out.parent.mkdir(parents=True)
    env = {**os.environ, "ROMCONV_ASSET_DIR": str(tmp_path / "legacy_assets")}
    env.pop("ROMCONV_PLAN", None)
    argv = [str(rom_dir / a[1:]) if a.startswith("@") else a for a in args]
    subprocess.run([sys.executable, f"internal/{tool}", *argv, str(out)], cwd=ROMCONV, env=env, check=True,
                   capture_output=True)
    return out.read_text()


def _new(ctx):
    return (ctx.paths.machines / "foo" / "x.h").read_text()


@pytest.mark.slow
@pytest.mark.parametrize("compress", [True, False])
def test_charmap_matches_legacy(tmp_path, log, compress):
    ctx = _ctx(tmp_path, log, "pacman.zip")
    old = _legacy(tmp_path, ctx, "tileconv.py", (*(["-c"] if compress else []), "pacman_tilemap", "@pacman.5e"),
                  roms=["pacman.5e"])
    tiles.charmap(ctx, "pacman_tilemap", "pacman.5e", "x.h", compress=compress)
    assert _new(ctx) == old
    assert "unsigned short" in old


@pytest.mark.slow
@pytest.mark.parametrize("compress", [True, False])
def test_spritemap_matches_legacy(tmp_path, log, compress):
    ctx = _ctx(tmp_path, log, "pacman.zip")
    old = _legacy(tmp_path, ctx, "spriteconv.py",
                  (*(["-c"] if compress else []), "pacman_sprites", "pacman", "@pacman.5f"), roms=["pacman.5f"])
    sprites.spritemap(ctx, "pacman_sprites", "pacman", ["pacman.5f"], "x.h", compress=compress)
    assert _new(ctx) == old


@pytest.mark.slow
@pytest.mark.parametrize("compress", [True, False])
def test_colormap_matches_legacy(tmp_path, log, compress):
    ctx = _ctx(tmp_path, log, "pacman.zip")
    old = _legacy(tmp_path, ctx, "cmapconv.py",
                  (*(["-c"] if compress else []), "pacman_colormap", "@82s123.7f", "0", "@82s126.4a"),
                  roms=["82s123.7f", "82s126.4a"])
    pal = cmap.palette(ctx, "82s123.7f")
    cmap.colormap(ctx, "pacman_colormap", "82s126.4a", pal[0:16], "x.h", compress=compress)
    assert _new(ctx) == old


@pytest.mark.slow
def test_dump_palette_matches_legacy(tmp_path, log):
    ctx = _ctx(tmp_path, log, "pacman.zip")
    old = _legacy(tmp_path, ctx, "cmapconv.py", ("pacman_colormaps", "@82s123.7f"), roms=["82s123.7f"])
    cmap.dump_palette(ctx, "pacman_colormaps", "x.h", cmap.palette(ctx, "82s123.7f"))
    assert _new(ctx) == old


@pytest.mark.slow
def test_emit_rom_compressed_matches_legacy(tmp_path, log):
    ctx = _ctx(tmp_path, log, "pacman.zip")
    names = ["pacman.6e", "pacman.6f", "pacman.6h", "pacman.6j"]
    old = _legacy(tmp_path, ctx, "romconv.py", ("-c", "pacman_rom", *("@" + n for n in names)), roms=names)
    emit_rom(ctx, "pacman_rom", names, "x.h", compress=True)
    assert _new(ctx) == old


@pytest.mark.slow
def test_emit_rom_plain_decode_matches_legacy(tmp_path, log):
    ctx = _ctx(tmp_path, log, "eyes.zip")
    names = ["d7", "e7", "f7", "h7"]
    old = _legacy(tmp_path, ctx, "romconv.py", ("-d", "eyes_rom", *("@" + n for n in names)), roms=names)
    emit_rom(ctx, "eyes_rom", names, "x.h", decode=True)
    assert _new(ctx) == old


@pytest.mark.slow
def test_emit_rom_patches_match_legacy(tmp_path, log):
    ctx = _ctx(tmp_path, log, "galaga.zip")
    names = ["gg1_1b.3p", "gg1_2b.3m", "gg1_3.2m", "gg1_4b.2l"]
    old = _legacy(tmp_path, ctx, "romconv.py", ("-p", "-c", "galaga_rom_cpu1", *("@" + n for n in names)),
                  roms=names)
    emit_rom(ctx, "galaga_rom_cpu1", names, "x.h", apply_patches=True, compress=True)
    assert _new(ctx) == old


@pytest.mark.slow
def test_decoded_charmap_and_sprites_match_legacy(tmp_path, log):
    # eyes: the data bit swap of both graphics ROMs
    ctx = _ctx(tmp_path, log, "eyes.zip")
    old = _legacy(tmp_path, ctx, "tileconv.py", ("-c", "eyes_tilemap", "@d5"), roms=["d5"])
    tiles.charmap(ctx, "eyes_tilemap", "d5", "x.h", compress=True)
    assert _new(ctx) == old
    old = _legacy(tmp_path, ctx, "spriteconv.py", ("-c", "eyes_sprites", "eyes", "@e5"), roms=["e5"])
    sprites.spritemap(ctx, "eyes_sprites", "eyes", ["e5"], "x.h", compress=True)
    assert _new(ctx) == old


@pytest.mark.slow
def test_frogger_family_matches_legacy(tmp_path, log):
    ctx = _ctx(tmp_path, log, "frogger.zip")
    old = _legacy(tmp_path, ctx, "tileconv.py", ("-c", "frogger_tilemap", "@frogger.606", "@frogger.607"),
                  roms=["frogger.606", "frogger.607"])
    tiles.charmap_frogger(ctx, "frogger_tilemap", ["frogger.606", "frogger.607"], "x.h", compress=True)
    assert _new(ctx) == old


@pytest.mark.slow
def test_dkong_colormap_matches_legacy(tmp_path, log):
    ctx = _ctx(tmp_path, log, "dkong.zip")
    roms = ["c-2k.bpr", "c-2j.bpr", "v-5e.bpr"]
    old = _legacy(tmp_path, ctx, "cmapconv.py",
                  ("-c", "dkong_colormap", "@c-2k.bpr", "@c-2j.bpr", "0", "@v-5e.bpr"), roms=roms)
    pal = cmap.palette(ctx, "c-2k.bpr", "c-2j.bpr")
    cmap.colormap_dkong(ctx, "dkong_colormap", "v-5e.bpr", pal, "x.h", compress=True)
    assert _new(ctx) == old


@pytest.mark.slow
def test_wavetable_matches_legacy(tmp_path, log):
    ctx = _ctx(tmp_path, log, "pacman.zip")
    old = _legacy(tmp_path, ctx, "audioconv.py", ("pacman_wavetable", "@82s126.1m"), roms=["82s126.1m"])
    wavetable.wavetable(ctx, "pacman_wavetable", ["82s126.1m"], "x.h")
    assert _new(ctx) == old


@pytest.mark.slow
@pytest.mark.parametrize("compress", [True, False])
def test_sample_matches_legacy(tmp_path, log, compress):
    if not (default().samples / "dkong_walk0.s8").is_file():
        pytest.skip("samples/dkong_walk0.s8 missing")
    ctx = _ctx(tmp_path, log, "dkong.zip")
    old = _legacy(tmp_path, ctx, "sampleconv.py",
                  (*(["-c"] if compress else []), "dkong_sample_walk0", "../samples/dkong_walk0.s8"))
    sample.sample(ctx, "dkong_sample_walk0", "dkong_walk0.s8", "x.h", compress=compress,
                  src_text="../samples/dkong_walk0.s8")
    assert _new(ctx) == old


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
