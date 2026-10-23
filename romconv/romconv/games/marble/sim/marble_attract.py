"""Marble Madness title screen model (Phase 6a, MARBLE.md "Attract"): the attract title with
the high score table, the playfield logo and the bouncing marbles (particles), also on the
final screen. Logic from Marble Love (particle-init-18cd2.ts, particle-bounce.ts,
palette-rng-fill-26cfa.ts, high-score-defaults.ts, hi-score-decode-41c8.ts,
late-game-logic-26f3e.ts type 0x2c); sequencer frame counts, title layout, logo map and the
logo palette overlay fitted to MAME captures.
"""
from romconv.games.marble import data as md

_FILL_TABLE = 0x20bb4   # 8 x 2 variants x 3 words, MO palette sets 0-7 pens 3-5
_FILL_FIXED = (0xafff, 0xcfc0)  # pens 1-2


def fill_palette(r, variants):
    """MO palette words 0x100 + set * 16 + pen (pens 1-5) of the fill."""
    pal = {}
    for k, v in enumerate(variants):
        a = _FILL_TABLE + k * 12 + (6 if v else 0)
        words = _FILL_FIXED + tuple(md.u16(r, a + 2 * i) for i in range(3))
        for i, w in enumerate(words):
            pal[0x100 + k * 16 + 1 + i] = w
    return pal


# --- high score table --------------------------------------------------------------------
_DEFAULTS = 0x1eea0           # 10 x 8 B: long score (low 24 bits), 3 ASCII initials, 0


def defaults(r):
    """[(initials, score)] in table order."""
    out = []
    for k in range(10):
        a = _DEFAULTS + 8 * k
        out.append((bytes(md.prog_bytes(r, a + 4, 3)).decode("ascii"), md.u32(r, a) & 0xffffff))
    return out
LOGO = tuple(map(tuple, (
    [0xf84] + [0xf85] * 18 + [0xf86],
    list(range(0xf87, 0xf9b)),
    [0xf87] + list(range(0xf9b, 0xfad)) + [0xf9a],
    [0xf87, 0] + list(range(0xfad, 0xfbd)) + [0, 0xf9a],
    [0xf87] + list(range(0xfbd, 0xfce)) + [0, 0xf9a],
    [0xfce] + [0xfcf] * 18 + [0xfd0],
)))
_LOGO_PAL = 0x20aca           # 13 words over playfield set 0 (palette set 0) at LOGO_PENS
_LOGO_PENS = (2, 4, 6, 8, 10, 12, 14, 18, 20, 22, 24, 26, 28)


def logo_palette(r):
    """32 playfield set 0 words of the title: palette set 0 with the overlay."""
    raw = {}
    for k in range(0xC3):
        raw[(md.u32(r, md.PAL_TARGETS + 4 * k) - 0xB00000) // 2] = md.u16(r, md.PAL_SETS + 2 * k)
    pal = [raw[md.PF_PALETTE + i] for i in range(32)]
    for k, pen in enumerate(_LOGO_PENS):
        pal[pen] = md.u16(r, _LOGO_PAL + 2 * k)
    return pal


def title_mo_palette(r):
    """MO palette words 0x100-0x17f of the title: palette set 0 (ROM)."""
    raw = {}
    for k in range(0xC3):
        raw[(md.u32(r, md.PAL_TARGETS + 4 * k) - 0xB00000) // 2] = md.u16(r, md.PAL_SETS + 2 * k)
    return {i: raw[i] for i in range(0x100, 0x180) if i in raw}
