from romconv.core.context import Context
from romconv.core.job import Job
from romconv.gfx import cmap
from romconv.gfx.rom import emit_rom
from romconv.gfx.sprites import spritemap
from romconv.gfx.tiles import charmap, tilemap_1942


def _convert(ctx: Context) -> None:
    # rom_cpu1, b1 stay PLAIN (flash): the Arena holds the tilemap instead,
    # which compresses 3x better (1942.cpp)
    emit_rom(ctx, "_1942_rom_cpu1", ["srb-03.m3", "srb-04.m4"], "1942_rom1.h")
    emit_rom(ctx, "_1942_rom_cpu1_b0", ["srb-05.m5"], "1942_rom1_b0.h", compress=True)
    emit_rom(ctx, "_1942_rom_cpu1_b1", ["srb-06.m6"], "1942_rom1_b1.h")
    emit_rom(ctx, "_1942_rom_cpu1_b2", ["srb-07.m7"], "1942_rom1_b2.h", compress=True)
    emit_rom(ctx, "_1942_rom_cpu2", ["sr-01.c11"], "1942_rom2.h", compress=True)

    charmap(ctx, "_1942_charmap", "sr-02.f2", "1942_charmap.h")
    tilemap_1942(ctx, "_1942_tilemap",
                 (("sr-08.a1", "sr-09.a2"), ("sr-10.a3", "sr-11.a4"), ("sr-12.a5", "sr-13.a6")),
                 "1942_tilemap.h", compress=True)

    spritemap(ctx, "_1942_sprites", "1942", ["sr-14.l1", "sr-15.l2", "sr-16.n1", "sr-17.n2"],
              "1942_spritemap.h")

    palette = cmap.palette(ctx, ("sb-5.e8", "sb-6.e9", "sb-7.e10"))
    cmap.colormap(ctx, "_1942_colormap_chars", "sb-0.f1", palette[128:144], "1942_character_cmap.h")
    cmap.colormap_1942_tiles(ctx, "_1942_colormap_tiles", ("sb-4.d6", "sb-3.d2", "sb-2.d1"), palette,
                             "1942_tile_cmap.h")
    cmap.colormap(ctx, "_1942_colormap_sprites", "sb-8.k3", palette[64:80], "1942_sprite_cmap.h")


JOB = Job(name="1942", convert=_convert, machine="1942", zips=("1942.zip",), plan=True)
