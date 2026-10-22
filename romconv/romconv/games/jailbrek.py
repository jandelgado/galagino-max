"""Convert the MAME jailbrek ROM set into galagino headers (MAME konami/jailbrek.cpp)."""
from types import MappingProxyType

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.emit.write import write_array, write_bytes
from romconv.games.gberet import decode_sprites, decode_tiles, write_cmap
from romconv.gfx.color import rgb565_swapped
from romconv.gfx.rom import rom_bytes, rom_bytes_all
from romconv.plan.budget import log_sizes

# MAME ROM_START( jailbrek )
_MAIN = (
    ("507p03.11d", "f999e382b9d3b812fca41f4d0da3ea692fef6b19"),
    ("507p02.9d", "c708b67c2d249448dae9a3d10c24d13ba6849597"),
)
_TILES = (
    ("507l08.4f", "c19a02a2def65648bf198fccec98ebbd2fc7c0fb"),
    ("507j09.5f", "b51a45dd5506bccdf0061dd6edd7f49ac86ed0f8"),
)
_SPRITES = (
    ("507j04.3e", "a10ddb405e884bfec521a3c7a29d22f63e535b59"),
    ("507j05.4e", "c42c064dbd7c5cf0b1d99651367e0bee1728a5b0"),
    ("507j06.5e", "22609489186dcb3d7cd49b7ddfdc6f04d0739354"),
    ("507j07.3f", "c0fd1e8d23c0f7e14c0b75f629448034420cf8ef"),
)
_PAL_RG = ("507j10.1f", "91eaa865375b3bc052897732b64b1ff7df3f78f6")
_PAL_B = ("507j11.2f", "bf77990260e8346faa3d3481718cbe46a4a27150")
_TILE_CLUT = ("507j13.7f", "972e9dab6c53722545dd3a43e3ada7921e88708b")
_SPR_CLUT = ("507j12.6f", "a8f21e86e6d974c9bfd92a147689d0e7316d66e2")
_VLM = ("507l01.8c", "d886b66d3861c3a90a1825ccf5bf0011831ca366")

_TILE_COUNT = 1024
_SPRITE_COUNT = 512
_VLM_SIZE = 0x2000


def _palette(rg, b):
    """32 colors, MAME pal4bit: red = low nibble and green = high nibble of rg, blue = low nibble of b."""
    return [rgb565_swapped((rg[i] & 15) * 0x11, (rg[i] >> 4) * 0x11, (b[i] & 15) * 0x11) for i in range(32)]


def _vlm_rom(data: bytes) -> bytes:
    """Speech ROM: the address mask is 0x1fff, both 8K halves must be equal."""
    if len(data) != 2 * _VLM_SIZE or data[:_VLM_SIZE] != data[_VLM_SIZE:]:
        raise ConversionError("jailbrek VLM ROM halves differ")
    return data[:_VLM_SIZE]


def _convert(ctx: Context) -> None:
    rom = rom_bytes_all(ctx, _MAIN)
    vlm = _vlm_rom(rom_bytes(ctx, _VLM))
    tiles = decode_tiles(rom_bytes_all(ctx, _TILES), _TILE_COUNT)
    sprites = decode_sprites(rom_bytes_all(ctx, _SPRITES), _SPRITE_COUNT)

    # encrypted Konami-1 opcodes, decrypted at run time
    write_bytes(ctx, "jailbrek_rom.h", "jailbrek_rom", rom, "6809 ROM 0x8000-0xffff, Konami-1 encrypted", __file__)
    write_bytes(ctx, "jailbrek_vlm.h", "jailbrek_vlm", vlm, "VLM5030 speech ROM", __file__)
    write_array(ctx, "jailbrek_tilemap.h", "jailbrek_tilemap", "uint32_t", "[8]", _TILE_COUNT, tiles,
                "1024 chars 8x8 4 bpp, row = 8 pens, pixel 0 in bits 31-28", __file__)
    write_array(ctx, "jailbrek_spritemap.h", "jailbrek_spritemap", "uint32_t", "[16][2]", _SPRITE_COUNT, sprites,
                "512 sprites 16x16 4 bpp, [row][0] pixels 0-7, [row][1] pixels 8-15, first in bits 31-28",
                __file__)
    write_cmap(ctx, "jailbrek", "Jail Break", _palette(rom_bytes(ctx, _PAL_RG), rom_bytes(ctx, _PAL_B)),
               rom_bytes(ctx, _TILE_CLUT), rom_bytes(ctx, _SPR_CLUT), ("507j13", "507j12"))

    log_sizes(ctx, {"rom": len(rom), "vlm": len(vlm), "tilemap": len(tiles) * 4, "spritemap": len(sprites) * 4})


JOB = Job(name="jailbrek", convert=_convert, machine="jailbrek", zips=("jailbrek.zip",), plan=True)
