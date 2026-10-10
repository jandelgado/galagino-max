#!/usr/bin/env python
import sys
import os

sys.path.insert(0, os.path.join("..", "pyconv"))
from asset_emit import emit_compressed, emit_plain, _hex_block
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


def create_output_file(rom_data, outfile, id_name, compress=True):
    """Write ROM data as a C header, compressed or PLAIN (flash)."""
    with open(outfile, "w") as of:
        of.write(f"// File generato automaticamente per {id_name}\n")
        if compress:
            emit_compressed(of, id_name, "unsigned char", "", len(rom_data), list(rom_data))
        else:
            emit_plain(of, id_name, "unsigned char", "", len(rom_data), "  " + _hex_block(rom_data))
    print(f"Written: {outfile}")


if __name__ == "__main__":
    game, rom_set = senjyo_sets.get(sys.argv)

    # main CPU: two 16 KB ROMs at 0x0000 and 0x4000
    main = b"".join(read_rom(name, 0x4000) for name in rom_set["main"])
    # PLAIN: compresses worst, so it leaves the Arena to the 16x16 sprites
    create_output_file(main, senjyo_sets.out_header(game, "main_cpu_rom"), f"{game}_main_cpu_rom",
                       compress=False)

    # sound CPU: one 8 KB ROM at 0x0000
    sub = read_rom(rom_set["sub"], 0x2000)
    create_output_file(sub, senjyo_sets.out_header(game, "sub_cpu_rom"), f"{game}_sub_cpu_rom")
