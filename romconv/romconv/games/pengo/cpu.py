from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.emit.asset import emit_compressed

# (pengo.zip name, name of older MAME versions, SHA-1) at 0x0000, 0x1000, ...
_CPU_ROMS = (
    ("epr-5128.ic8", "epr5128.u8", "a387b72501da77bf38b58619d2099083a0463e1f"),
    ("epr-5129.ic7", "epr5129.u7", "d1c66bb9cf479e6960dbcd35c820097a81eaa555"),
    ("epr-5130.ic15", "epr5130.u15", "a8a568da68babd0ccb9f2cee4182fc01c3138494"),
    ("epr-5131a.ic14", "epr5131.u14", "2ee821b0f6e0f3cfeae7f5ff25a6e9bd977efce0"),
    ("epr-5132.ic21", "ep5124.21", "fdebc68a6d87f8ecdf52a57a34ae5ae844a13510"),
    ("epr-5133.ic20", "epr5133.u20", "ed814d58318c1055e475ff678609d189727bf9b4"),
    ("epr-5134.ic32", "ep5126.32", "0ac5ffdad7bdcb32e630b9582e1b1aaece5198c9"),
    ("epr-5135a.ic31", "epr5135.u31", "332b484d47c9921ed93432755bb2d7a9d4628939"),
)
# (pengo.zip name, name in older MAME sets, SHA-1)
_WAVE_ROM = ("pr1635.ic51", "pr1635.51", "563c9770028fe39188e62630711589d6ed242a66")
_WAVE_SOURCE = "pr1635.51"  # as the header comment shows it
_WAVE_SIZE = 256
_WAVES = 8
_WAVE_LEN = 32


def convert_cpu(ctx: Context) -> None:
    all_rom_data = bytearray()
    for name, old_name, sha1 in _CPU_ROMS:
        all_rom_data.extend(ctx.rom([name, old_name], sha1))

    with ctx.header("pengo_rom.h") as f:
        f.write("// File generato da cpu_conv.py\n")
        f.write("// ROM per: Pengo Main CPU ROM (set 'pengo')\n")
        f.write(f"// Dimensione totale: {len(all_rom_data)} bytes\n\n")
        emit_compressed(f, "pengo_rom", "unsigned char", "", len(all_rom_data), list(all_rom_data))


def convert_wavetable(ctx: Context) -> None:
    wave_data = ctx.rom(_WAVE_ROM[:2], _WAVE_ROM[2])
    if len(wave_data) != _WAVE_SIZE:
        raise ConversionError(f"{_WAVE_ROM[0]} is {len(wave_data)} bytes, expected {_WAVE_SIZE}")

    with ctx.header("pengo_wavetable.h") as of:
        of.write("// Wavetable audio per Pengo\n")
        of.write(f"// ROM di origine: {_WAVE_SOURCE}\n\n")
        of.write("#pragma once\n\n")
        of.write("const signed char pengo_wavetable[][32] = {\n")

        for w in range(_WAVES):
            of.write(f"  // Waveform #{w}\n")

            # ASCII drawing of the waveform
            for y in range(8):
                of.write("  //")
                for s in range(_WAVE_LEN):
                    val = wave_data[_WAVE_LEN * w + s]
                    if val == 15 - 2 * y:
                        of.write("---")
                    elif val == 15 - (2 * y + 1):
                        of.write("___")
                    else:
                        of.write("   ")
                of.write("\n")

            # 0..15 unsigned centered around zero (-7..8), as the audio player expects
            of.write("  {")
            for s in range(_WAVE_LEN):
                of.write(f"{wave_data[_WAVE_LEN * w + s] - 7:3d}")
                if s != _WAVE_LEN - 1:
                    of.write(",")

            of.write(" }")
            if w != _WAVES - 1:
                of.write(",")

            of.write("\n\n")

        of.write("};\n")
