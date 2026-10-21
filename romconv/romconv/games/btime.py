# ============================================================
# Burger Time (Data East 1982) ROM converter
# Converts "btime.zip" (Data East set 1)
# ============================================================
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.emit.write import write_bytes
from romconv.hw.btime import decode_chars, decode_tiles16

_MAINCPU = (("aa04.9b", "ed3f3712423979dcb351941fa85dce6a0a7bb16b"),
            ("aa06.13b", "8c77397e934907bc47a739f263196a0f2f81ba3d"),
            ("aa05.10b", "d0da4e360039f6a8d8142a4e8e05c1f90c0af68a"),
            ("aa07.15b", "4a32bc92f8ff5fbe112f56e62d2c03da8851a7b9"))
_AUDIOCPU = ("ab14.12h", "27940026d0c6212d1138d2fd88880df697218627")
_GFX1 = (("aa12.7k", "24204d591aa2c264a852ee9ba8c4be63efd97728"),
         ("ab13.9k", "e64b6381a9298eaf74e79fa5f1ea8e9596c58a49"),
         ("ab10.10k", "3d2ecfd54a5a9d68b53cf4b4ee1f2daa6aef2123"),
         ("ab11.12k", "0a55b091cd4e7f317c35defe13d5051b26042eee"),
         ("aa8.13k", "d9b1ee2d1f2fd66705d497c80252861b49aa9254"),
         ("ab9.15k", "b72633de6268ce16742bba4dcba835df860d6c2f"))
_GFX2 = (("ab00.1b", "6a0a8e6b7860859f22daa33634e34fbf91387659"),
         ("ab01.3b", "4abdcbd4f3362c3e4463a1274731289f1a72d2e6"),
         ("ab02.4b", "4a03bf011dc1fb2902f42587b1174b880cf06df1"))
_BG_MAP = ("ab03.6b", "737af6e264183a1f151f277a07cf250d6abb3fd8")


# ------------------------------------------------------------
def _write_char_tiles(ctx, tiles):
    with ctx.header("burgertime_chartiles.h") as f:
        print("// Burger Time char set #1 (aa12.7k+ab13.9k+ab10.10k+ab11.12k+aa8.13k+ab9.15k)", file=f)
        print("// 1024 tile 8x8 3bpp (valori pixel 0-7). pen0 = trasparente quando il", file=f)
        print("// tilemap speciale e' attivo (m_bnj_scroll[0]&0x10), opaco altrimenti.", file=f)
        print("// Colore SEMPRE fisso a palette RAM[0..7] (color group 0, vedi btime.cpp).", file=f)
        flat = [v for t in tiles for y in range(8) for v in t[y]]
        emit_compressed(f, "burgertime_chartiles", "unsigned char", "[8][8]", len(tiles), flat)


def _write_sprite_tiles(ctx, tiles):
    with ctx.header("burgertime_spritetiles.h") as f:
        print("// Burger Time sprites (STESSA ROM del char set #1, gfx1, granularita'", file=f)
        print("// 16x16 invece di 8x8 -- tile16layout). 256 sprite 16x16 3bpp.", file=f)
        print("// pen0 = trasparente (transpen ultimo parametro 0 in btime.cpp).", file=f)
        print("// Colore SEMPRE fisso a palette RAM[0..7] (color group 0).", file=f)
        print("// COMPRESSED: fits Arena alongside burgertime_chartiles/rom_main/", file=f)
        print("// bgtiles/bgmap/rom_audio -- see ARENA BUDGET note in burgertime.cpp.", file=f)
        flat = [v for t in tiles for y in range(16) for v in t[y]]
        emit_compressed(f, "burgertime_spritetiles", "unsigned char", "[16][16]", len(tiles), flat)


def _write_bg_tiles(ctx, tiles):
    with ctx.header("burgertime_bgtiles.h") as f:
        print("// Burger Time sfondo (ab00.1b+ab01.3b+ab02.4b, gfx2, tile16layout).", file=f)
        print("// 64 tile 16x16 3bpp (valori pixel 0-7). Layer OPAQUE (mai trasparente,", file=f)
        print("// vedi gfxdecode->gfx(2)->opaque in draw_background). Colore SEMPRE", file=f)
        print("// fisso a palette RAM[8..15] (color group base 8, vedi GFXDECODE_ENTRY).", file=f)
        flat = [v for t in tiles for y in range(16) for v in t[y]]
        emit_compressed(f, "burgertime_bgtiles", "unsigned char", "[16][16]", len(tiles), flat)


def _write_bg_map(ctx, data):
    write_bytes(ctx, "burgertime_bgmap.h", "burgertime_bgmap", data,
               "Burger Time bg_map lookup ROM (ab03.6b), 0x800 byte grezzi: "
               "4 banchi x 0x200 selezionabili da m_btime_tilemap[i&3], ogni "
               "banco indicizzato 0..0xff, valore = indice diretto in burgertime_bgtiles "
               "(vedi draw_background() in btime.cpp)")


# ------------------------------------------------------------
def _convert(ctx: Context) -> None:
    cpu01, cpu02, cpu03, cpu04 = (ctx.rom(*f) for f in _MAINCPU)
    audio = ctx.rom(*_AUDIOCPU)
    gfx1_files = [ctx.rom(*f) for f in _GFX1]
    gfx2_files = [ctx.rom(*f) for f in _GFX2]
    bg_map = ctx.rom(*_BG_MAP)

    gfx1 = bytearray().join(gfx1_files)
    assert len(gfx1) == 0x6000
    char_tiles, char_count = decode_chars(gfx1)
    _write_char_tiles(ctx, char_tiles)
    ctx.log.info(f"char tiles: {char_count}")

    sprite_tiles, sprite_count = decode_tiles16(gfx1)
    _write_sprite_tiles(ctx, sprite_tiles)
    ctx.log.info(f"sprite tiles: {sprite_count}")

    # --- gfx2 (background): 3 files = 0x1800 byte ---
    gfx2 = bytearray().join(gfx2_files)
    assert len(gfx2) == 0x1800
    bg_tiles, bg_count = decode_tiles16(gfx2)
    _write_bg_tiles(ctx, bg_tiles)
    ctx.log.info(f"bg tiles: {bg_count}")

    _write_bg_map(ctx, bg_map)

    # --- ROM CPU (rimangono CIFRATE in flash, decrittate a runtime dal core
    # m6502 patchato con l'hook fetch - vedi decocpu7.cpp) ---
    # maincpu: solo 0xc000-0xffff popolato in questo set (16KB, 4 file);
    # 0xb000-0xbfff resta a 0 (non presente nel set "btime" Data East set 1).
    maincpu = bytearray(0x1000) + cpu01 + cpu02 + cpu03 + cpu04
    assert len(maincpu) == 0x5000  # 0xb000-0xffff
    write_bytes(ctx, "burgertime_rom_main.h", "burgertime_rom_main", bytes(maincpu),
               "Burger Time main CPU ROM 0xb000-0xffff (0xb000-0xbfff empty in this"
               "set), encrypted DECO CPU-7 -- runtime decryption (see burgertime.cpp "
               "fetch_cpu7)")

    write_bytes(ctx, "burgertime_rom_audio.h", "burgertime_rom_audio", audio,
               "Burger Time audio CPU ROM 0xe000-0xefff (mirror 0x1000-0x1fff), not encrypted")

    ctx.log.info("Burger Time conversion finished.")


JOB = Job(name="btime", convert=_convert, machine="burgertime", zips=("btime.zip",))
