from romconv.core.context import Context
from romconv.emit.asset import emit_compressed

# ROM_LOAD offsets of the main CPU ROMs (bombjack.cpp). 0x8000-0xbfff is RAM and I/O: 0xff.
_MAIN_ROMS = (
    ("09_j01b.bin", 0x0000),
    ("10_l01b.bin", 0x2000),
    ("11_m01b.bin", 0x4000),
    ("12_n01b.bin", 0x6000),
    ("13.1r", 0xC000),
)
_CPU_REGION_SIZE = 0x10000
_ROM_SIZE = 0x2000
# Only the 8 KB chip is emitted. The rest of MAME's 16 KB "audiocpu" region
# is 0xFF fill, returned by bombjack.cpp's read handlers instead.
_AUDIO_ROM_SIZE = 0x2000


def convert_main_cpu(ctx: Context) -> None:
    # 0xFF is unmapped memory
    cpu_memory = bytearray([0xFF] * _CPU_REGION_SIZE)
    for name, start in _MAIN_ROMS:
        cpu_memory[start:start + _ROM_SIZE] = ctx.rom(name)

    with ctx.header("bombjack_rom1.h") as f:
        f.write("// File generato automaticamente per bombjack_rom_cpu1\n")
        f.write("// Contiene le ROM della CPU principale di Bomb Jack\n")
        emit_compressed(f, "bombjack_rom_cpu1", "unsigned char", "", len(cpu_memory), list(cpu_memory))


def convert_audio_cpu(ctx: Context) -> None:
    rom_data = ctx.rom("01_h03t.bin")
    assert len(rom_data) == _AUDIO_ROM_SIZE

    audio_memory = bytearray([0xFF] * _AUDIO_ROM_SIZE)
    audio_memory[0x0000:_AUDIO_ROM_SIZE] = rom_data

    with ctx.header("bombjack_rom2.h") as f:
        f.write("// File generato automaticamente per bombjack_rom_cpu2\n")
        f.write("// Contiene la ROM della CPU Audio di Bomb Jack\n")
        emit_compressed(f, "bombjack_rom_cpu2", "unsigned char", "", len(audio_memory), list(audio_memory))


def convert_bg_maps(ctx: Context) -> None:
    rom_data = ctx.rom("02_p04t.bin")
    # MAME reloads the 4 KB ROM into its 8 KB region: duplicate the data
    final_rom_data = rom_data + rom_data

    with ctx.header("bombjack_bg_maps.h") as f:
        f.write("// File generato automaticamente per la mappa del background di Bomb Jack.\n")
        f.write("// Dati estratti da: ../../roms/02_p04t.bin\n\n")
        emit_compressed(f, "bombjack_bg_maps", "unsigned char", "", len(final_rom_data), list(final_rom_data))
