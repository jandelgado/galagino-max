"""Convert the MAME ccastlesj (joystick) ROM set, clone of ccastles, into galagino headers."""
from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.emit.write import write_array, write_bytes
from romconv.gfx.gfxutil import mame_decode
from romconv.gfx.rom import rom_bytes
from romconv.plan.budget import check_budget

_CLONE = "ccastlesj.zip"
_PARENT = "ccastles.zip"

# (zip, name, sha1), MAME ROM_START( ccastlesj )
_MAIN = (
    (_CLONE, "a000.12m", "fbd6a5a0e4e865421ed3720aa61221d03583f248"),       # 0xa000
    (_CLONE, "c000.13m", "ade2c63656339c3e7e634470a17bc30da1006979"),       # 0xc000
    (_CLONE, "e000.14m", "076caffe67910bdcd1f51a41f2cc4ebdb930c7ca"),       # 0xe000
    (_PARENT, "136022-102.1h", "69c3da2cbefc5e03a77357e817e3015da5d8334a"),  # 0x10000
    (_PARENT, "136022-101.1f", "81fa95b4d9beacb06d6b4afdf346d94117396557"),  # 0x12000
)
_GFX = (
    (_PARENT, "136022-106.8d", "01c279edee322cc28f34506c312e4a9e3363b1be"),
    (_PARENT, "136022-107.8b", "82bdf764ac23e72598883283c5e957169387abd4"),
)
_SYNC = (_PARENT, "82s129-136022-108.7k", "c3f3e4e7f313ecfd101cc52dfc44bd6b51a2ac88")
_WPROM = (_PARENT, "82s129-136022-110.11l", "ae155918fdafd14299bc448b43eed8ad9c1ef5ef")
_PRIPROM = (_PARENT, "82s129-136022-111.10k", "278bf61a290ae72ddaae2bafb4ab6739d3fb6238")

_VRAM_BYTES = 32768  # Arena, allocated by the machine


def _load(ctx, entry):
    zipname, name, sha1 = entry
    return rom_bytes(ctx, (name, sha1), zipname)


def _check_sync(sync):
    # ccastles.h hardcodes vblank lines 0..23 and IRQs at 0, 64, 128, 192
    vblank = [y for y in range(256) if sync[y] & 1]
    irq = [y for y in range(256) if not sync[(y - 1) & 255] & 8 and sync[y] & 8]
    if vblank != list(range(24)) or irq != [0, 64, 128, 192]:
        raise ConversionError(f"sync PROM differs: vblank {vblank}, irq {irq}")


def _sprite_words(gfx):
    # ccastles_spritelayout: 8x16, planes {4, RGN_FRAC(1,2)+0, RGN_FRAC(1,2)+4}
    half = len(gfx) * 8 // 2
    sprites = mame_decode(gfx, 8, 16, [4, half, half + 4], [0, 1, 2, 3, 8, 9, 10, 11],
                          [y * 16 for y in range(16)], 32 * 8, 256)
    words = []
    for s in sprites:
        for row in s:
            words.append(sum(p << (4 * i) for i, p in enumerate(row)))
    return words


def _convert(ctx: Context) -> None:
    rom = b"".join(_load(ctx, e) for e in _MAIN)
    assert len(rom) == 0xa000
    gfx = b"".join(_load(ctx, e) for e in _GFX)
    _check_sync(_load(ctx, _SYNC))
    proms = _load(ctx, _WPROM) + _load(ctx, _PRIPROM)
    words = _sprite_words(gfx)

    write_bytes(ctx, "ccastles_rom.h", "ccastles_rom", rom,
                "M6502 ROM: 0x0000 = CPU 0xa000-0xffff (bank 0), 0x6000 = bank 1 for 0xa000-0xdfff.", __file__)
    write_array(ctx, "ccastles_sprites.h", "ccastles_sprites", "uint32_t", "[16]", len(words) // 16, words,
                "256 8x16 sprites, uint32_t per row, pixel i in bits 4i..4i+2, pen 7 transparent.", __file__)
    write_bytes(ctx, "ccastles_proms.h", "ccastles_proms", proms,
                "0x000 write protect PROM 110, 0x100 priority PROM 111.", __file__)

    check_budget(ctx, len(rom) + len(words) * 4 + len(proms) + _VRAM_BYTES)


JOB = Job(name="ccastles", convert=_convert, machine="ccastles", zips=(_CLONE, _PARENT))
