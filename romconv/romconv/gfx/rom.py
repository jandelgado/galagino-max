from collections.abc import Sequence
from types import MappingProxyType

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.emit.asset import emit_compressed, emit_plain
from romconv.emit.text import hex_block
from romconv.gfx.bits import bit_permute_step

# symbol -> (address, expected byte, new byte)
_PATCHES = MappingProxyType({
    "galaga_rom_cpu1": (
        # jump over tile ram test
        (0x3382, 0x06, 0xc3),   # 06, jp
        (0x3383, 0x0a, 0x35),   # xx35
        (0x3384, 0xd9, 0x34),   # 34xx
        # only one ramtest round, instead of 30
        (0x348a, 0x1e, 0x01),
        # skip rom test
        (0x352b, 0xe5, 0xc9),   # ret
    ),
})

# first bytes of the frogger audio cpu rom with D0 and D1 swapped
_FROGGER_SWAPPED = bytes([0x05, 0x00, 0x22, 0x00, 0x40, 0xc3, 0x0b, 0x02])


def emit_rom(ctx: Context, symbol: str, roms: Sequence[str], header: str, *, apply_patches: bool = False,
             decode: bool = False, compress: bool = False, machine: str | None = None) -> None:
    """Concatenate the ROM files into one byte array asset in `header`.

    apply_patches: the PATCHES of the symbol. decode: Pacman family data
    bit swap. compress: COMPRESSED instead of PLAIN."""
    offset = 0
    all_bytes = bytearray()

    for name in roms:
        rom_data = ctx.rom(name)

        # the first frogger audio cpu rom has bits
        # d0 and d1 swapped. Fix this
        if rom_data[:8] == _FROGGER_SWAPPED:
            rom_data = bytearray((b & 0xfc) | ((b & 2) >> 1) | ((b & 1) << 1) for b in rom_data)

        if apply_patches:
            for address, expected, new in _PATCHES.get(symbol, ()):
                if address - offset < len(rom_data):
                    if rom_data[address - offset] != expected:
                        raise ConversionError(f"{name}: unexpected patch data at {address:#x}")
                    ctx.log.info(f"Patching {hex(address)} : {expected} -> {new}")
                    rom_data[address - offset] = new

        offset += len(rom_data)

        if decode:
            rom_data = bytearray(bit_permute_step(b, 8, 2) for b in rom_data)

        all_bytes.extend(rom_data)

    with ctx.header(header, machine=machine) as f:
        if compress:
            emit_compressed(f, symbol, "unsigned char", "", len(all_bytes), list(all_bytes))
        else:
            emit_plain(f, symbol, "unsigned char", "", len(all_bytes), "  " + hex_block(all_bytes))
