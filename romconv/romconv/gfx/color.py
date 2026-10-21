"""RGB565 encodings of the display, one function per distinct formula.

All return the 16 bit value the ILI9341 expects (high and low byte swapped)
unless named _be."""


def swap_bytes(v: int) -> int:
    return ((v & 0x00FF) << 8) | ((v & 0xFF00) >> 8)


def rgb565_be(r: int, g: int, b: int) -> int:
    """Standard RGB565 from 0..255 channels, shifted down and clamped."""
    r, g, b = (max(0, min(255, c)) for c in (r, g, b))
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def rgb565_swapped(r: int, g: int, b: int) -> int:
    """From 0..255 channels, shifted down and clamped."""
    return swap_bytes(rgb565_be(r, g, b))


def rgb565_swapped_scaled(r: int, g: int, b: int) -> int:
    """From 0..255 channels, scaled (31/255, 63/255) instead of shifted."""
    return swap_bytes(((r * 31 // 255) << 11) + ((g * 63 // 255) << 5) + (b * 31 // 255))


def rgb565_swapped_4bit(r: int, g: int, b: int) -> int:
    """From 4 bit PROM channels (separate red, green and blue PROMs)."""
    return swap_bytes(((r << 1) << 11) + ((g << 2) << 5) + (b << 1))


def rgb565_swapped_packed(c: int) -> int:
    """From one bbgggrrr byte (Galaga, Pac-Man, Frogger)."""
    b = 31 * ((c >> 6) & 0x3) // 3
    g = 63 * ((c >> 3) & 0x7) // 7
    r = 31 * ((c >> 0) & 0x7) // 7
    return swap_bytes((r << 11) + (g << 5) + b)


def rgb565_swapped_dkong(c: int) -> int:
    """From one inverted rrrgggbb byte (Donkey Kong)."""
    r = 31 - 31 * ((c >> 5) & 0x7) // 7
    g = 63 - 63 * ((c >> 2) & 0x7) // 7
    b = 31 - 31 * ((c >> 0) & 0x3) // 3
    return swap_bytes((r << 11) + (g << 5) + b)
