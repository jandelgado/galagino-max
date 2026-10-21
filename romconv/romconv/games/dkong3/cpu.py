from romconv.core.context import Context
from romconv.emit.asset import emit_compressed

# ROM_LOAD offsets of the main CPU ROMs (dkong.cpp, dkong3j). 0x6000-0x7fff is empty.
_MAIN_ROMS = (
    ("dk3c.7b", 0x0000),
    ("dk3c.7c", 0x2000),
    ("dk3c.7d", 0x4000),
    ("dk3c.7e", 0x8000),
)
_ROM_SIZE = 0x2000
_CPU_REGION_SIZE = 0x10000

# (rom, symbol, comment)
_SOUND_ROMS = (
    ("dk3c.5l", "dkong3_rom_sound_a", "Sound CPU #1 (RP2A03A)"),
    ("dk3c.6h", "dkong3_rom_sound_b", "Sound CPU #2 (RP2A03B)"),
)


def convert_cpu(ctx: Context) -> None:
    # 0xFF is unmapped memory; the whole 64 KB address space is exported
    cpu_memory = bytearray([0xFF] * _CPU_REGION_SIZE)
    for name, start in _MAIN_ROMS:
        cpu_memory[start:start + _ROM_SIZE] = ctx.rom(name)

    with ctx.header("dkong3_rom.h") as f:
        f.write("// File generato automaticamente per dkong3_rom_cpu\n")
        emit_compressed(f, "dkong3_rom_cpu", "unsigned char", "", len(cpu_memory), list(cpu_memory))


def convert_sound(ctx: Context) -> None:
    with ctx.header("dkong3_sound_roms.h") as f:
        f.write("// File header generato automaticamente per le Sound CPU di dkong3j\n")
        f.write("// Contiene le ROM per entrambe le CPU sonore.\n\n")

        for name, symbol, comment in _SOUND_ROMS:
            rom_data = ctx.rom(name)
            f.write(f"// --- {comment} ---\n")
            f.write(f"// Dati dal file: dkong3.zip: {name}\n")
            emit_compressed(f, symbol, "unsigned char", "", len(rom_data), list(rom_data))
            f.write("\n")
