from collections.abc import Sequence


def BIT(value: int, shift: int) -> int:
    return (value >> shift) & 1


def bit_permute_step(x: int, m: int, shift: int) -> int:
    t = ((x >> shift) ^ x) & m
    return (x ^ t) ^ (t << shift)


def decode_data(data: Sequence[int]) -> list[int]:
    """Pacman family address/data bit swap of the eyes and mrtnt graphics ROMs."""
    out = list(data)
    for i in range(0, len(out), 8):
        swapped = [out[i + bit_permute_step(j, 1, 2)] for j in range(8)]
        for j in range(8):
            out[i + j] = bit_permute_step(swapped[j], 16, 2)
    return out
