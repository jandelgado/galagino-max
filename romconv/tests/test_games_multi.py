import hashlib

import pytest

from romconv import registry
from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.games import bombjack, commando, dkong3, dkongjr, gng, invaders, mrdo, pengo, starforce
from romconv.games.bombjack import tiles as bombjack_tiles
from romconv.games.dkong3 import cmap as dkong3_cmap
from romconv.games.mrdo import palette as mrdo_palette
from romconv.games.pengo import cpu as pengo_cpu
from romconv.games.starforce import cpu as starforce_cpu
from romconv.games.starforce import tiles as starforce_tiles
from romconv.games.starforce.sets import SETS


def _ctx(job, paths, log):
    return Context(job, paths, (), log)


def test_job_shapes():
    jobs = {j.name: j for j in (bombjack.JOB, commando.JOB, dkong3.JOB, dkongjr.JOB, gng.JOB, invaders.JOB,
                                mrdo.JOB, pengo.JOB, *starforce.JOBS)}
    assert {n for n, j in jobs.items() if j.plan} == {"bombjack", "commando", "gng", "starforce", "baluba"}
    assert jobs["invaders"].machine == "spaceinvaders"
    assert jobs["dkongjr"].deps == ("dkong",) and jobs["pengo"].deps == ("pacman",)
    assert jobs["baluba"].deps == ("starforce",) and jobs["baluba"].zips == ("baluba.zip",)
    assert jobs["pengo"].zips == ("pengo.zip",)


def test_registered_and_partial_run_pulls_bases():
    names = {j.name for j in registry.ALL}
    assert {"bombjack", "commando", "dkong3", "dkongjr", "gng", "invaders", "mrdo", "pengo", "starforce",
            "baluba"} <= names
    assert {"starforce", "baluba"} <= {j.name for j in registry.resolve(["baluba"])}


def test_starforce_sets_have_matching_regions():
    for spec in SETS.values():
        assert len(spec.main) == 2 and len(spec.fg) == len(spec.bg1) == len(spec.bg2) == 3
        assert len(spec.bg3) == len(spec.sprites) == 3


def test_starforce_palette_endpoints():
    assert starforce_cpu._palette_color(0) == 0
    # intensity 3 and full colors: 4 bit 15 on each channel
    assert starforce_cpu._palette_color(0xFF) == 0xFFFF


def test_starforce_bg_tile_planes_first_rom_is_msb():
    # one 16x16 tile, 32 bytes per plane: only the first plane has pixel (0, 0) set
    plane0 = bytearray(32)
    plane0[0] = 0x80
    tiles = starforce_tiles._decode_bg_tiles([plane0, bytearray(32), bytearray(32)], 1)
    assert tiles[0][0][0] == 0b100 and tiles[0][0][1] == 0


def test_starforce_rom_size_is_checked(paths, log, make_zip):
    make_zip("baluba.zip", {"0": b"\0" * 10})
    ctx = _ctx(starforce.JOBS[1], paths, log)
    with pytest.raises(ConversionError, match="expected 16384"):
        starforce_cpu.convert_cpu(ctx, "baluba", SETS["baluba"])


def test_bombjack_fg_tile_rotation_and_planes():
    planes = [bytearray(8 * 512) for _ in range(3)]
    planes[0][0] = 0x80   # 03_e08t is the MSB plane: pixel (0, 0) = 4
    tile = bombjack_tiles._decode_fg_tile(planes, 0)
    assert tile[0][0] == 4 and tile[0][1] == 0
    rotated = bombjack_tiles._rotate_90_cw(tile)
    assert rotated[0][0] == tile[7][0]


def test_dkong3_palette_tiles_inverted_sprites_not():
    zeros = bytearray(512)
    pal = dkong3_cmap._master_palette(zeros, zeros)
    assert len(pal) == 512 and pal[0] == 0xFFFF and pal[256] == 0
    with pytest.raises(ConversionError):
        dkong3_cmap._master_palette(bytearray(4), zeros)


def test_mrdo_weights_match_mame():
    # weights of MAME palette_init; 1, 2 and 4 go negative before the clamp
    assert mrdo_palette._weights() == [0, 0, 0, 88, 0, 112, 133, 192, 60, 150, 166, 212, 180, 221, 229, 255]


def test_mrdo_master_rgb_indexes_t2_low_u2_high():
    u2, t2 = bytearray(32), bytearray(32)
    t2[0], u2[0] = 0x03, 0x0c
    assert mrdo_palette._master_rgb(u2, t2)[0] == (88, 180, 0)
    assert mrdo_palette._master_rgb(u2, t2)[1] == (0, 0, 0)


def test_mrdo_sprite_pen_zero_is_transparent():
    prom = bytes(range(32))
    group = mrdo_palette._sprite_group([0x1234] * 256, prom, 3)
    assert group[0] == 0 and len(group) == 4


def test_pengo_cpu_rom_names_the_missing_member(paths, log, make_zip):
    make_zip("pengo.zip", {"epr-1640.ic92": b"x"})
    with pytest.raises(ConversionError) as err:
        pengo_cpu.convert_cpu(_ctx(pengo.JOB, paths, log))
    assert "pengo.zip" in str(err.value) and "epr-5128.ic8" in str(err.value)


def test_pengo_cpu_rom_hash_is_checked(paths, log, make_zip):
    make_zip("pengo.zip", {"epr-5128.ic8": b"not the rom"})
    with pytest.raises(ConversionError, match="SHA-1 mismatch"):
        pengo_cpu.convert_cpu(_ctx(pengo.JOB, paths, log))


def test_invaders_rom_is_the_four_files_in_order(paths, log, make_zip):
    make_zip("invaders.zip", {f"invaders.{c}": bytes([i]) * 0x800 for i, c in enumerate("hgfe")})
    invaders._convert_rom(_ctx(invaders.JOB, paths, log))
    text = (paths.machines / "spaceinvaders" / "spaceinvaders_rom.h").read_text()
    assert text.startswith("// Space Invaders CPU ROM") and "#endif" in text


def test_commando_sid_must_exist_and_match(paths, log):
    ctx = _ctx(commando.JOB, paths, log)
    with pytest.raises(ConversionError, match="Commando.sid"):
        commando._convert_sid(ctx)
    paths.romszip.mkdir(parents=True)
    (paths.romszip / "Commando.sid").write_bytes(b"tune")
    with pytest.raises(ConversionError, match="SHA-1 mismatch"):
        commando._convert_sid(ctx)


def test_gng_sid_hash_is_checked(paths, log):
    paths.romszip.mkdir(parents=True)
    (paths.romszip / "Ghosts_n_Goblins.sid").write_bytes(b"tune")
    with pytest.raises(ConversionError, match="SHA-1 mismatch"):
        gng._convert_sid(_ctx(gng.JOB, paths, log))


def test_dkong3_sprite_palette_uses_the_shared_rgb565():
    from romconv.gfx.color import rgb565_swapped
    prom = bytearray(512)
    prom[256] = 0xF0    # sprite colors are not inverted: red 15
    assert dkong3_cmap._master_palette(prom, bytearray(512))[256] == rgb565_swapped(255, 0, 0)


def test_gng_packing():
    tile = [[x & 0xF for x in range(16)] for _ in range(16)]
    words = gng._pack_4bpp(tile)
    assert len(words) == 32 and words[0] == 0x76543210 and words[1] == 0xFEDCBA98
    assert gng._pack_2bpp([[1, 2, 3, 0]]) == [0b00111001]
    assert gng._byte_body(bytes(17)).count("\n") == 1


def test_gng_known_hashes_are_sha1():
    assert all(len(h) == 40 and int(h, 16) >= 0 for h in gng._SHA1.values())
    assert hashlib.sha1(b"").hexdigest() not in gng._SHA1.values()


def test_pengo_cpu_prefers_the_japanese_set(paths, log, make_zip):
    jp = {name: b"x" for name, _ in pengo_cpu._JAPAN_ROMS}
    make_zip("pengo.zip", jp)
    ctx = _ctx(pengo.JOB, paths, log)
    with pytest.raises(ConversionError, match="pengoj/epr-5120.ic8: SHA-1 mismatch"):
        pengo_cpu.convert_cpu(ctx)
