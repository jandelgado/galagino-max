"""Decode helpers for Namco-family hardware (Galaga/Mappy/Xevious/Tower of Druaga lineage)."""


def nudge(v: int) -> int:
    # 0 is the transparency marker; map true black to near-black.
    return v if v != 0 else 0x2000


def pal_rgb(c: int) -> tuple[int, int, int]:
    return (255 * ((c >> 0) & 7) // 7, 255 * ((c >> 3) & 7) // 7, 255 * ((c >> 6) & 3) // 3)
