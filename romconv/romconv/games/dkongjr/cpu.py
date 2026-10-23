from romconv.core.context import Context
from romconv.emit.asset import emit_compressed

_CPU_REGION_SIZE = 0x10000
_USED_SIZE = 0x6000
# (rom, [(rom offset, size, cpu address)]) per MAME's ROM_LOAD / ROM_CONTINUE
_MAIN_ROMS = (
    ("c_5ba.bin", ((0x0000, 0x1000, 0x0000), (0x1000, 0x1000, 0x3000))),
    ("c_5ca.bin", ((0x0000, 0x0800, 0x2000), (0x0800, 0x0800, 0x4800),
                   (0x1000, 0x0800, 0x1000), (0x1800, 0x0800, 0x5800))),
    ("c_5ea.bin", ((0x0000, 0x0800, 0x4000), (0x0800, 0x0800, 0x2800),
                   (0x1000, 0x0800, 0x5000), (0x1800, 0x0800, 0x1800))),
)


def convert_cpu(ctx: Context) -> None:
    # 0xFF is unmapped memory
    cpu_memory = bytearray([0xFF] * _CPU_REGION_SIZE)
    for name, parts in _MAIN_ROMS:
        rom_data = ctx.rom(name)
        for offset, size, address in parts:
            cpu_memory[address:address + size] = rom_data[offset:offset + size]

    final_rom_data = cpu_memory[:_USED_SIZE]
    with ctx.header("dkongjr_rom1.h") as f:
        f.write("// File generato automaticamente per dkongjr_rom1\n")
        emit_compressed(f, "dkongjr_rom1", "unsigned char", "", len(final_rom_data), list(final_rom_data))


def convert_sound(ctx: Context) -> None:
    rom_data = ctx.rom("c_3h.bin")

    with ctx.header("dkongjr_rom2.h") as f:
        f.write("// File header generato automaticamente per la Sound CPU di dkongjrj\n")
        f.write("// Contiene i dati del file: dkongjrj.zip: c_3h.bin\n\n")
        emit_compressed(f, "dkongjr_rom2", "unsigned char", "", len(rom_data), list(rom_data))
