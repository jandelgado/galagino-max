"""POKEY poly counter tables (port of MAME pokey_device::poly_init_4_5 / poly_init_9_17)
for source/src/emulation/pokey_poly.h."""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.write import banner


def _poly_4_5(size: int) -> list[int]:
    mask = (1 << size) - 1
    lfsr, out = 0, []
    for _ in range(mask):
        lfsr = (lfsr << 1) | (~((lfsr >> 2) ^ (lfsr >> (size - 1))) & 1)
        out.append(lfsr & mask)
    return out


def _poly_9() -> list[int]:
    lfsr, out = 0x1ff, []
    for _ in range(0x1ff):
        bit = (lfsr & 1) ^ ((lfsr >> 5) & 1)
        lfsr = (lfsr >> 1) | (bit << 8)
        out.append(lfsr)
    return out


def _mask(states: list[int]) -> int:
    return sum((s & 1) << i for i, s in enumerate(states))


def _convert(ctx: Context) -> None:
    p4, p5, p9 = _poly_4_5(4), _poly_4_5(5), _poly_9()
    out = ctx.paths.src / "emulation" / "pokey_poly.h"
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(banner(__file__))
        f.write("// POKEY poly counters (MAME poly_init_4_5 / poly_init_9_17), poly17 in pokey.cpp.\n")
        f.write("#ifndef POKEY_POLY_H\n#define POKEY_POLY_H\n#include <stdint.h>\n")
        f.write("// output bit 0 of each state, LSB first\n")
        f.write(f"static const uint16_t pokey_poly4_bits = 0x{_mask(p4):04x}; // 15 entries\n")
        f.write(f"static const uint32_t pokey_poly5_bits = 0x{_mask(p5):08x}; // 31 entries\n")
        f.write("// full 9 bit states: bit 0 = output, low byte = RANDOM in poly9 mode\n")
        f.write("static const uint16_t pokey_poly9[511] = {\n")
        for i in range(0, len(p9), 16):
            f.write("  " + ",".join(f"0x{v:03x}" for v in p9[i:i + 16]) + ",\n")
        f.write("};\n#endif\n")
    ctx.log.info(f"wrote {out}")


JOB = Job(name="pokey_poly", convert=_convert, always=True)
