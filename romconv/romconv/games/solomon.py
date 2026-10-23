"""Convert the MAME solomon ROM set into galagino headers (MAME tecmo/solomon.cpp)."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_plain
from romconv.emit.write import write_array, write_asset, write_bytes
from romconv.gfx.gfxutil import mame_decode, pack_nibbles, step
from romconv.gfx.rom import rom_bytes, rom_bytes_all
from romconv.plan import layout
from romconv.plan.budget import log_sizes


# MAME ROM_START( solomon )
_MAIN_0000 = ("6.3f", "b911aa157ad94aa539dbe329a197d8902860c809")
_MAIN_4000_8000 = ("7.3h", "3b6a8dde72cddf95438539c5dc4622c95f932a04")
_MAIN_F000 = ("8.3jk", "101acaa19b779cb8b4fffddbe63fe011c7d4b6e9")
_AUDIO = ("1.3jk", "713036c0a80b623086aa674bb5f8a135b6fedb01")
_FG = (("12.3t", "27302898c64330870c47025e61bd5acbd9483865"),
      ("11.3r", "2e070c0fd5b9d7eb9b7e0d53f25b1a5063ef3095"))
_BG = (("10.3p", "8cc87ab8faacdb1973791d207bb25ea9b444b66d"),
      ("9.3m", "0fc3b4a0bd2b17b79e2d1f7d4fe445c09ce4e730"))
_SPRITES = (("2.5lm", "8e7a78186473a6b5c42577ac9e4591ee2d1151f2"),
           ("3.6lm", "8eaf3150568c407bd8dc1cdf874b8417e5cca3d2"),
           ("4.7lm", "e29ffb9fcff50ce982d5e502e10a8e29a4c47390"),
           ("5.8lm", "1c7a01dab056ec7d787a6f55772b9fa6fe67305a"))

_HALF = 0x4000
_HIGH_ROM = 0x1000  # 8.3jk bytes mapped at f000


def _main_image(ctx):
    # 0000-3fff 6.3f; 7.3h first half -> 8000, ROM_CONTINUE -> 4000;
    # 8.3jk first 0x1000 -> f000 (ROM_IGNORE), stored at image offset c000
    h = rom_bytes(ctx, _MAIN_4000_8000)
    return rom_bytes(ctx, _MAIN_0000) + h[_HALF:] + h[:_HALF] + rom_bytes(ctx, _MAIN_F000)[:_HIGH_ROM]


def _decode_tiles(rom):
    # gfx_8x8x4_packed_msb
    tiles = mame_decode(rom, 8, 8, step(0, 1, 4), step(0, 4, 8), step(0, 32, 8), 256, 2048)
    return [pack_nibbles(row) for t in tiles for row in t]


def _decode_sprites(rom):
    # spritelayout: bitplanes 512 * 32 * 8 bits apart, plane 0 = MSB
    planes = step(0, 512 * 32 * 8, 4)
    xoffs = step(0, 1, 8) + step(64, 1, 8)
    yoffs = step(0, 8, 8) + step(128, 8, 8)
    sprites = mame_decode(rom, 16, 16, planes, xoffs, yoffs, 256, 512)
    return [[[pack_nibbles(row[0:8]), pack_nibbles(row[8:16])] for row in s] for s in sprites]


def _bytes_body(data):
    return ",\n".join("  " + ",".join(f"0x{b:02x}" for b in data[i:i + 16])
                      for i in range(0, len(data), 16))


def _sprites_body(sprites):
    def sprite(s):
        return "{" + ",".join(f"{{0x{a:08x},0x{b:08x}}}" for a, b in s) + "}"
    return ",\n".join("  " + sprite(s) for s in sprites)


def _convert(ctx: Context) -> None:
    arena_block_a, arena_block_b = layout.read_layouts(layout.header_path(ctx.paths))["local"]
    rom = _main_image(ctx)
    audio = rom_bytes(ctx, _AUDIO)
    fg = _decode_tiles(rom_bytes_all(ctx, _FG))
    bg = _decode_tiles(rom_bytes_all(ctx, _BG))
    sprites = _decode_sprites(rom_bytes_all(ctx, _SPRITES))
    assert len(rom) == 0xd000 and len(audio) == 0x4000

    # PLAIN: no Arena room left after both tile sets (bg + audio fill block A,
    # fg block B); the main ROM compresses badly and sprites are read least
    write_asset(ctx, "solomon_main_rom.h", "main Z80 0000-bfff, f000-ffff at offset c000, flash",
                lambda f: emit_plain(f, "solomon_main_rom", "unsigned char", "", len(rom), _bytes_body(rom)),
                __file__)
    write_asset(ctx, "solomon_sprites.h",
                "512 sprites 16x16 4 bpp, [row][0] px 0-7, [row][1] px 8-15, first in bits 31-28, flash",
                lambda f: emit_plain(f, "solomon_sprites", "uint32_t", "[16][2]", 512, _sprites_body(sprites)),
                __file__)
    write_bytes(ctx, "solomon_audio_rom.h", "solomon_audio_rom", audio, "sound Z80 0000-3fff", __file__)
    write_array(ctx, "solomon_fg_tiles.h", "solomon_fg_tiles", "uint32_t", "[8]", 2048, fg,
                "2048 fg chars 8x8 4 bpp, pixel 0 in bits 31-28", __file__)
    write_array(ctx, "solomon_bg_tiles.h", "solomon_bg_tiles", "uint32_t", "[8]", 2048, bg,
                "2048 bg chars 8x8 4 bpp, pixel 0 in bits 31-28", __file__)

    # unpack order in the ctor: bg -> A, fg -> B, audio -> A
    sizes = {"bg": len(bg) * 4, "fg": len(fg) * 4, "audio": len(audio)}
    log_sizes(ctx, sizes, arena_block_a + arena_block_b)
    assert sizes["bg"] + sizes["audio"] <= arena_block_a and sizes["fg"] <= arena_block_b
    ctx.log.info(f"  flash PLAIN: main {len(rom)}, sprites {len(sprites) * 128}")


JOB = Job(name="solomon", convert=_convert, machine="solomon", zips=("solomon.zip",))
