"""Decode helpers for Namco-family hardware (Galaga/Mappy/Xevious/Tower of Druaga lineage)."""


def nudge(v: int) -> int:
    # 0 is the transparency marker; map true black to near-black.
    return v if v != 0 else 0x2000

