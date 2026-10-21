"""Convert the MAME missile (rev 3) ROM set into galagino headers."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.write import write_bytes
from romconv.gfx.rom import rom_bytes, rom_bytes_all

# (names, sha1), MAME ROM_START( missile ), CPU 0x5000-0x7fff
_MAIN = (
    (("035820-02.h1",), "9a39978138dc28fdefe193bfae1b226391e471db"),
    (("035821-02.jk1",), "0916925d3c94d766d33f0e4badf6b0add835d748"),
    (("035822-03e.kl1",), "2deb1219223032a9c83114e4e8b2fc11a570754c"),
    (("035823-02.ln1", "035823-02.lm1"), "d0f22894f779c74ceef644c9f03d840d9545efea"),
    (("035824-02.np1",), "9718f84a73c66b4e8ef7805a7ab638a7380624e1"),
    (("035825-02.r1",), "0339a6ce6744d2091cc7e07675e509b202b0f380"),
)
_WPROM = (("035826-01.l6",), "2beebf7855e29849ada1823eae031fc98220bc43")


def _convert(ctx: Context) -> None:
    rom = rom_bytes_all(ctx, _MAIN)
    assert len(rom) == 0x3000
    data = rom + rom_bytes(ctx, _WPROM)
    write_bytes(ctx, "missile_rom.h", "missile_rom", data,
                "M6502 ROM: 0x0000 = CPU 0x5000-0x7fff, 0x3000 = write PROM 035826-01 (32 bytes).", "missile")
    ctx.log.info(f"  Arena budget: {len(data)} / 182000 bytes (rom {len(rom)}, prom 32); VRAM in memory[]")


JOB = Job(name="missile", convert=_convert, machine="missile", zips=("missile.zip",))
