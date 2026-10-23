from romconv.core.context import Context
from romconv.emit.asset import emit_compressed
from romconv.emit.write import module_path

_ROMS = ("a4-01.bin", "c4-02.bin", "e4-03.bin", "f4-04.bin")


def convert_cpu(ctx: Context) -> None:
    all_rom_data = bytearray()
    for name in _ROMS:
        all_rom_data.extend(ctx.rom(name))

    with ctx.header("mrdo_rom1.h") as f:
        f.write(f"// File generato da {module_path(__file__)}\n")
        f.write(f"// Dimensione totale: {len(all_rom_data)} bytes\n\n")
        emit_compressed(f, "mrdo_rom1", "unsigned char", "", len(all_rom_data), list(all_rom_data))
