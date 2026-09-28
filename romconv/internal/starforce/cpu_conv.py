#!/usr/bin/env python
import sys
import os

sys.path.insert(0, os.path.join("..", "pyconv"))
from asset_emit import emit_compressed
from convutil import fatal
import senjyo_sets


def read_rom(name, size):
    path = senjyo_sets.rom(name)
    if not os.path.exists(path):
        fatal(f"ROM file not found: '{path}'")
    with open(path, "rb") as f:
        data = f.read()
    if len(data) != size:
        fatal(f"'{path}': size {len(data)}, expected {size}")
    return data


def create_output_file(rom_data, outfile, id_name):
    """Write ROM data as a compressed C header."""
    with open(outfile, "w") as of:
        of.write(f"// File generato automaticamente per {id_name}\n")
        emit_compressed(of, id_name, "unsigned char", "", len(rom_data), list(rom_data))
    print(f"Written: {outfile}")


if __name__ == "__main__":
    game, rom_set = senjyo_sets.get(sys.argv)

    # main CPU: two 16 KB ROMs at 0x0000 and 0x4000
    main = b"".join(read_rom(name, 0x4000) for name in rom_set["main"])
    create_output_file(main, senjyo_sets.out_header(game, "main_cpu_rom"), f"{game}_main_cpu_rom")

    # sound CPU: one 8 KB ROM at 0x0000
    sub = read_rom(rom_set["sub"], 0x2000)
    create_output_file(sub, senjyo_sets.out_header(game, "sub_cpu_rom"), f"{game}_sub_cpu_rom")
