from romconv.core.context import Context
from romconv.core.job import Job
from romconv.gfx import cmap
from romconv.gfx.rom import emit_rom
from romconv.gfx.sample import sample
from romconv.gfx.sprites import spritemap
from romconv.gfx.tiles import charmap_frogger


def _convert(ctx: Context) -> None:
    emit_rom(ctx, "dkong_rom_cpu1", ["c_5et_g.bin", "c_5ct_g.bin", "c_5bt_g.bin", "c_5at_g.bin"], "dkong_rom1.h",
             compress=True)
    emit_rom(ctx, "dkong_rom_cpu2", ["s_3i_b.bin", "s_3j_b.bin"], "dkong_rom2.h", compress=True)
    charmap_frogger(ctx, "dkong_tilemap", ["v_5h_b.bin", "v_3pt.bin"], "dkong_tilemap.h", compress=True)
    spritemap(ctx, "dkong_sprites", "dkong", ["l_4m_b.bin", "l_4n_b.bin", "l_4r_b.bin", "l_4s_b.bin"],
              "dkong_spritemap.h", compress=True)
    cmap.colormap_dkong(ctx, "dkong_colormap", "v-5e.bpr", cmap.palette(ctx, "c-2k.bpr", "c-2j.bpr"),
                        "dkong_cmap.h", compress=True)

    # Arena has room: compressed
    for name in ("walk0", "walk1", "walk2", "jump", "stomp", "fall"):
        sample(ctx, f"dkong_sample_{name}", f"dkong_{name}.s8", f"dkong_sample_{name}.h", compress=True)


JOB = Job(name="dkong", convert=_convert, machine="dkong", zips=("dkong.zip",))
