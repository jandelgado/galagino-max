# ============================================================
# Bump 'n' Jump (Data East 1982, set "bnj")
# ============================================================
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.gfx.gfxutil import mame_decode, rot_galagino
from romconv.hw.btime import decode_chars, decode_tiles16

_MAINCPU = (("ad08.12b", "83105718c2d18ef75ca18ae92b34545cb939bc02"),
            ("ad07.12c", "f62d752bb7a995e120ed4d642793c543f0ef13ca"),
            ("ad06.12d", "f231ed008537aeeeacbec64f485e9a96ab3441e1"))
_AUDIOCPU = ("ad05.6c", "1279d564e65fd3ccac25b1f9fbb40d910de2b544")
_GFX1 = (("ad00.4e", "cacf71fa6c0f7121d077381a0ff6222f534295ab"),
         ("ad01.4f", "5e52554f594f569527af4768d244cc40a7b4460a"),
         ("ad02.4h", "e98f0eb476b8f033f5cc70a6e503afc4e651fd45"))
_GFX2 = (("ad03.10e", "b356512d2ebd4e2005e76496b434e5ecebadb251"),
         ("ad04.10f", "49d5f9c0b695f474197fbb761bacc065b6b5808a"))

# bnj_tile16layout (btime.cpp righe 2121-2130), NUOVO in questo progetto,
# usato SOLO per lo sfondo scrollabile di bnj (regione gfx2, 0x2000 byte =
# bnj10e.bin+bnj10f.bin, ciascuno 0x1000):
#
#   bnj_tile16layout = { 16,16, RGN_FRAC(1,2), 3,
#     { RGN_FRAC(1,2)+4, RGN_FRAC(0,2)+0, RGN_FRAC(0,2)+4 },
#     { STEP4(3*16*8,1), STEP4(2*16*8,1), STEP4(1*16*8,1), STEP4(0*16*8,1) },
#     { STEP16(0,8) }, 64*8 }
#
# planeoffset: plane0 = meta'-alta regione (bnj10f) NIBBLE ALTO (+4);
# plane1 = meta'-bassa regione (bnj10e) NIBBLE BASSO (+0); plane2 =
# meta'-bassa regione (bnj10e) NIBBLE ALTO (+4). Il nibble basso di
# bnj10f (meta'-alta) NON e' usato da questo layout (3bpp, non 4bpp).
# xoffset: 4 gruppi da 4 colonne, in ordine INVERTITO (col 12-15, poi 8-11,
# poi 4-7, poi 0-3) -- ogni gruppo di 4 pixel adiacenti condivide lo stesso
# nibble ma bit diversi (STEP4 con incremento 1 = 4 bit consecutivi nello
# stesso nibble). charincrement 64*8=512 bit = 64 byte/tile.
def _bnj_bg_layout_decode(gfx2):
    total_bits = len(gfx2) * 8
    half_bits = total_bits // 2
    planes = [half_bits + 4, 0 + 0, 0 + 4]
    xoffs = ([3 * 16 * 8 + i for i in range(4)] +
             [2 * 16 * 8 + i for i in range(4)] +
             [1 * 16 * 8 + i for i in range(4)] +
             [0 * 16 * 8 + i for i in range(4)])
    yoffs = [y * 8 for y in range(16)]
    bits_per_tile = 64 * 8
    count = half_bits // bits_per_tile
    return mame_decode(gfx2, 16, 16, planes, xoffs, yoffs, bits_per_tile, count), count


# ------------------------------------------------------------
def _write_rom(ctx, name, sym, data, comment):
    with ctx.header(name) as f:
        print(f"// {comment}", file=f)
        emit_compressed(f, sym, "unsigned char", "", len(data), list(data))


def _write_char_tiles(ctx, tiles):
    with ctx.header("bnj_chartiles.h") as f:
        print("// Bump 'n' Jump char set #1 (gfx1).", file=f)
        print("// 1024 tile 8x8 3bpp (pixel values 0-7). Colors from ", file=f)
        print("// palette RAM[0..7] (color group 0).", file=f)
        flat = [v for t in tiles for y in range(8) for v in t[y]]
        emit_compressed(f, "bnj_chartiles", "unsigned char", "[8][8]", len(tiles), flat)


def _write_sprite_tiles(ctx, tiles):
    with ctx.header("bnj_spritetiles.h") as f:
        print("// Bump 'n' Jump sprites", file=f)
        print("// (gfx1 16x16 -- tile16layout). 256 sprite 16x16 3bpp.", file=f)
        print("// COMPRESSED: fits Arena alongside bnj_chartiles/bnj_rom_main/", file=f)
        print("// bnj_bgtiles/bnj_rom_audio -- see ARENA BUDGET note in bnj.cpp.", file=f)
        flat = [v for t in tiles for y in range(16) for v in t[y]]
        emit_compressed(f, "bnj_spritetiles", "unsigned char", "[16][16]", len(tiles), flat)


def _write_bg_tiles(ctx, tiles):
    with ctx.header("bnj_bgtiles.h") as f:
        print("// Bump 'n'Jump background", file=f)
        print("// gfx2 bnj_tile16layout -- nibble packing", file=f)
        print("// converter). 16x16 3bpp, layer OPAQUE", file=f)
        print("// colors from palette RAM[8..15]", file=f)
        flat = [v for t in tiles for y in range(16) for v in t[y]]
        emit_compressed(f, "bnj_bgtiles", "unsigned char", "[16][16]", len(tiles), flat)


# ------------------------------------------------------------
def _convert(ctx: Context) -> None:
    cpu01, cpu02, cpu03 = (ctx.rom(*f) for f in _MAINCPU)
    audio = ctx.rom(*_AUDIOCPU)
    gfx1_files = [ctx.rom(*f) for f in _GFX1]
    gfx2_files = [ctx.rom(*f) for f in _GFX2]

    gfx1 = bytearray().join(gfx1_files)
    assert len(gfx1) == 0x6000
    char_tiles, char_count = decode_chars(gfx1)
    _write_char_tiles(ctx, char_tiles)
    ctx.log.info(f"char tiles: {char_count}")

    sprite_tiles, sprite_count = decode_tiles16(gfx1)
    _write_sprite_tiles(ctx, sprite_tiles)
    ctx.log.info(f"sprite tiles: {sprite_count}")

    gfx2 = bytearray().join(gfx2_files)
    assert len(gfx2) == 0x2000
    bg_tiles_raw, bg_count = _bnj_bg_layout_decode(gfx2)
    bg_tiles = [rot_galagino(t) for t in bg_tiles_raw]
    _write_bg_tiles(ctx, bg_tiles)
    ctx.log.info(f"bg tiles: {bg_count}")

    # --- ROM CPU (rimangono CIFRATE in flash: DECO C10707 e' STATICA,
    # decifrata a runtime dal core m6502 patchato via hook fetch, stesso
    # meccanismo di btime/CPU-7 ma bitswap fisso senza stato) ---
    maincpu = cpu01 + cpu02 + cpu03
    assert len(maincpu) == 0x6000  # 0xa000-0xffff
    _write_rom(ctx, "bnj_rom_main.h", "bnj_rom_main", maincpu,
               "Bump 'n' Jump main CPU ROM 0xa000-0xffff (24KB), DECO C10707 "
               "(bitswap bit5<->bit6 for all fetch opcode.")

    _write_rom(ctx, "bnj_rom_audio.h", "bnj_rom_audio", audio,
               "Bump 'n' Jump audio CPU ROM 0xe000-0xefff (mirrored 0x1000-0x1fff)")

    ctx.log.info("Bump'n'Jump (ROM/gfx) conversion done.")


JOB = Job(name="bnj", convert=_convert, machine="bnj", zips=("bnj.zip",), plan=True, deps=("btime",))
