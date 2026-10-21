from collections.abc import Sequence

from romconv.core.context import Context
from romconv.core.errors import ConversionError


def wavetable(ctx: Context, symbol: str, roms: Sequence[str], header: str) -> None:
    """Namco WSG waveforms: 8 waves of 32 signed samples per 256 byte PROM."""
    with ctx.header(header) as f:
        print("const signed char " + symbol + "[][32] = {", file=f)

        for rom in roms:
            wave_data = ctx.rom(rom)

            if len(wave_data) != 256:
                raise ConversionError(f"{rom}: missing rom data")

            # 8 waveforms per rom
            for w in range(8):
                print("// " + rom + " wave #{:d}".format(w), file=f)
                # draw waveform
                for y in range(8):
                    print("//", end="", file=f)
                    for s in range(32):
                        if wave_data[32 * w + s] == 15 - 2 * y:
                            print("---", end="", file=f)
                        elif wave_data[32 * w + s] == 15 - (2 * y + 1):
                            print("___", end="", file=f)
                        else:
                            print("   ", end="", file=f)
                    print("", file=f)

                print(" {", end="", file=f)
                # with 32 values each
                for s in range(32):
                    print("{:2d}".format(wave_data[32 * w + s] - 7), end="", file=f)
                    if s != 31:
                        print(",", end="", file=f)
                    elif w != 7 or rom != roms[-1]:
                        print("},", file=f)
                    else:
                        print("}", file=f)

                print("", file=f)

        print("};", file=f)
