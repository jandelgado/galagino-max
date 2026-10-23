"""Commando (Capcom, 1985; MAME capcom/commando.cpp), with the C64 Commando SID tune as music."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.emit.write import banner
from romconv.gfx.color import rgb565_swapped_4bit
from romconv.gfx.rom import emit_rom
from romconv.gfx.sprites import spritemap
from romconv.gfx.tiles import charmap, tilemap_1942
from romconv.misc.userfile import read_user_file

_TILES_A = 418  # commando.h TILES_A
# (low, high) ROM of each 256 sprite bank
_SPRITE_BANKS = (("vt05.7e", "vt08.7h"), ("vt06.8e", "vt09.8h"), ("vt07.9e", "vt10.9h"))
_PROMS = (  # red, green, blue
    ("vtb1.1d", "8b057f6e26155dd9e48bde182e680fce4519f600"),
    ("vtb2.2d", "ca6dddca98baf00a65b2fb70b69cf4704ef8c831"),
    ("vtb3.3d", "644ac17c7413f094ec9a15cba87bbd421b26321f"),
)
_SID_FILE = "Commando.sid"
_SID_SHA1 = "8549166918b51bfde88f3f059588c4618dfbfc75"


def _convert_cmap(ctx: Context) -> None:
    """256 direct RGB444 colors from vtb1/2/3 (no lookup PROMs), split per layer as MAME's
    gfxdecode does: tiles 0-127 (16 x 8), sprites 128-191 (4 x 16), chars 192-255 (16 x 4)."""
    r, g, b = (ctx.rom(name, sha1) for name, sha1 in _PROMS)
    pal = [rgb565_swapped_4bit(r[i] & 15, g[i] & 15, b[i] & 15) for i in range(256)]

    with ctx.header("commando_cmap.h") as f:
        f.write(banner(__file__))
        emit_compressed(f, "commando_cmap_tiles", "unsigned short", "[8]", 16, pal[0:128])
        emit_compressed(f, "commando_cmap_sprites", "unsigned short", "[16]", 4, pal[128:192])
        emit_compressed(f, "commando_cmap_chars", "unsigned short", "[4]", 16, pal[192:256])


def _convert_sid(ctx: Context) -> None:
    """The Rob Hubbard tune, user supplied like the ROMs (HVSC MUSICIANS/H/Hubbard_Rob/Commando.sid)."""
    data = read_user_file(ctx, _SID_FILE, _SID_SHA1, "HVSC Hubbard_Rob/Commando.sid")

    with ctx.header("commando_sid.h") as out:
        out.write(banner(__file__))
        out.write("// C64 Commando PSID (Rob Hubbard, 1985 Elite), whole file\n")
        emit_compressed(out, "commando_sid", "unsigned char", "", len(data), list(data))
    ctx.log.info(f"commando_sid.h: {len(data)} bytes")


def _convert(ctx: Context) -> None:
    # The ROMs and bg tiles 0..TILES_A-1 stay PLAIN (flash): sprite bank 1,
    # chars and the SID gain more per Arena byte than those tiles.
    # CPU code: encrypted opcodes, decrypted at run time
    emit_rom(ctx, "commando_rom", ["cm04.9m", "cm03.8m"], "commando_rom.h")
    emit_rom(ctx, "commando_sound", ["cm02.9f"], "commando_sound.h")

    charmap(ctx, "commando_chars", "vt01.5d", "commando_chars.h")
    # split at TILES_A (commando.h): part a in flash, part b fills block B
    tilemap_1942(ctx, "commando_tiles",
                 (("vt11.5a", "vt12.6a"), ("vt13.7a", "vt14.8a"), ("vt15.9a", "vt16.10a")),
                 "commando_tiles.h", split=_TILES_A, plain=True)

    # one asset per 256 sprite bank
    for bank, (low, high) in enumerate(_SPRITE_BANKS):
        spritemap(ctx, f"commando_sprites{bank}", "1942", [low, high], f"commando_sprites{bank}.h")

    _convert_cmap(ctx)
    _convert_sid(ctx)


JOB = Job(name="commando", convert=_convert, machine="commando", zips=("commando.zip",), plan=True)
