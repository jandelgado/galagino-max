"""Marble Madness HUD model (Phase 3b, MARBLE.md "HUD"): the alpha layer (0xa03000, 64 x 32
words, font 136032.104.f5) and its 32 palette words, built from a compact HUD state instead
of the 68k text OS. Texts are the ROM strings, big glyph tile runs are captured from MAME
(the BIOS big font tables 0x72ac/0x0ccc are not ported). Palette pulses: anim ticks 0x26bee
(+0x70, entry 3, score fade) and 0x26c78 (+0x71, entry 11, timer flash); bonus palette
0x118d2.

The state comes from MAME frame traces here (hud_states) and from the work RAM image and
the flow sequencer in the remake (marble_hud.cpp). Arcade coordinates (alpha) are checked
against MAME; the remake shows the same items moved into 30 x 36 cells (view).
"""
from types import MappingProxyType

from romconv.games.marble import data as md

_ROWS, COLS = 32, 64            # alpha RAM
_VIEW_ROWS, _VIEW_COLS = 36, 30  # remake grid (240 x 288)
OPAQUE = 0x2000

# ROM strings (NUL terminated, ' ' = code 0)
_S_YOU, _S_THE, _S_BONUS_FIN, _S_SEC, _S_DIED, _S_TOTAL = 0x230a0, 0x230b4, 0x230c8, 0x230e8, 0x230f8, 0x23114
_S_BLANK26, _S_TIME_TO, _S_EXTRA, S_RACES, _S_WRONG, _S_BLANK31 = 0x2317c, 0x23198, 0x231ec, 0x23224, 0x232ec, 0x2330c
S_NAMES = (0x2332c, 0x23336, 0x23340, 0x2334e, 0x23356, 0x2335c)
_S_BONUS_FOR, _S_TIME_LEFT, _S_OUT, _S_OVER = 0x233ae, 0x233c0, 0x2370a, 0x2371c
RACE_STRIDE = 0x1c

PULSE = 0x20b34                # 16 words, the same at 0x20b74 (timer flash)
_BONUS_PAL = MappingProxyType({0: 0, 4: 0, 8: 0, 12: 0, 9: 0xafff, 13: 0xafff, 29: 0})  # 0x118d2

# big font (2 rows of tiles; digits 0x100 + 4d: TL TR / BL BR, comma one column)
_BLANK, _COMMA_LO, _COLON = 0x13c, 0x16e, 0x16d
BIG = MappingProxyType({   # captured from MAME (alpha RAM, final screen)
    "CONGRATULATIONS!": (
        (0x100, 0x12f, 0x100, 0x101, 0x148, 0x149, 0x100, 0x12f, 0x12c, 0x121, 0x128, 0x129, 0x152,
         0x153, 0x131, 0x154, 0x131, 0x142, 0x128, 0x129, 0x152, 0x153, 0x139, 0x13a, 0x100, 0x101,
         0x148, 0x149, 0x14e, 0x14f, 0x16f),
        (0x102, 0x130, 0x102, 0x103, 0x14a, 0x14b, 0x102, 0x136, 0x12d, 0x141, 0x12a, 0x12b, 0x106,
         0x107, 0x155, 0x156, 0x132, 0x143, 0x12a, 0x12b, 0x106, 0x107, 0x106, 0x13b, 0x102, 0x103,
         0x14a, 0x14b, 0x150, 0x151, 0x170)),
    "FINAL": (
        (0x12c, 0x133, 0x139, 0x13a, 0x148, 0x149, 0x128, 0x129, 0x131, 0x142),
        (0x12d, 0x135, 0x106, 0x13b, 0x14a, 0x14b, 0x12a, 0x12b, 0x132, 0x143)),
    "SCORE:": (
        (0x14e, 0x14f, 0x100, 0x12f, 0x100, 0x101, 0x12c, 0x121, 0x12c, 0x133, 0x16d),
        (0x150, 0x151, 0x102, 0x130, 0x102, 0x103, 0x12d, 0x141, 0x12d, 0x134, 0x16d)),
})

# screen bits
PAL, TOP, INTRO, SILLY, BONUS, BONUS_PALETTE, OUT, FIN = (1 << k for k in range(8))
_TITLE = 1 << 9
DEMO = 1 << 10   # attract demo race: coin box, banner (Hud.banner)
NO_BANNER = 0xff


def font(r):
    """512 glyphs, 8 rows of 8 pens 0-3 (anlayout: planes at bits 0 and 4, x bits 0-3, 8-11)."""
    d = r.alpha
    xs = (0, 1, 2, 3, 8, 9, 10, 11)
    bit = lambda g, n: (d[g * 16 + (n >> 3)] >> (7 - (n & 7))) & 1
    return [[[bit(g, y * 16 + x) << 1 | bit(g, y * 16 + 4 + x) for x in xs] for y in range(8)]
            for g in range(512)]


def rom_str(r, addr):
    s = bytearray()
    while md.prog_bytes(r, addr + len(s), 1)[0]:
        s.append(md.prog_bytes(r, addr + len(s), 1)[0])
    return s.decode()


def word(code, colour, opaque):
    return code | colour << 10 | (OPAQUE if opaque else 0)


def _text(s, colour, opaque):
    return [word(0 if ch == " " else ord(ch), colour, opaque) for ch in s]


def _commas(v):
    return "{:,}".format(v)


def _big_digits(s, colour, opaque=False):
    """Big number string (digits, commas, spaces) -> two rows of words."""
    top, bot = [], []
    for ch in s:
        if ch == ",":
            top.append(_BLANK); bot.append(_COMMA_LO)
        elif ch == " ":                  # digit wide
            top += [_BLANK, _BLANK]; bot += [_BLANK, _BLANK]
        else:
            t = 0x100 + 4 * int(ch)
            top += [t, t + 1]; bot += [t + 2, t + 3]
    return [word(c, colour, opaque) for c in top], [word(c, colour, opaque) for c in bot]


class Hud:
    """Compact HUD state (marble_video::Hud in the remake)."""
    def __init__(self, **kw):
        self.screen, self.score, self.seconds, self.value = 0, 0, 0, 0
        self.race, self.add, self.deaths = 0, 0, 0
        self.pal3 = self.pal11 = 0xff   # pulse table index, 0xff = ROM colour
        self.table, self.stage = (), 0     # title: [(initials, score)], frames since start
        self.banner = NO_BANNER            # demo: banner number (0x242aa), NO_BANNER none
        self.__dict__.update(kw)

    def key(self):
        return tuple(sorted(self.__dict__.items()))


def items(h, r):
    """Alpha items of state h: (dx, dy, row, col, words) in arcade coordinates; dx/dy move them
    into the remake's 30 x 36 view (banners: arcade screen 3 rows lower, centred)."""
    s = h.screen
    out = []
    put = lambda dx, dy, row, col, ws: out.append((dx, dy, row, col, ws))
    if s & TOP:  # first: the bonus line covers a timer border cell, as in the arcade
        border = word(0x5f, 7, False)
        put(-3, 0, 0, 3, [border] * 5)
        put(-3, 0, 1, 3, _text("SCORE", 0, True))
        put(-3, 0, 2, 3, _text(_commas(h.score), 0, True))
        top, bot = _big_digits("%02d" % max(0, min(h.seconds, 99)), 2, True)
        put(-6, 0, 0, 19, [border] * 4 + [word(0xff, 7, False)])
        put(-6, 0, 1, 19, top + [word(0xdf, 7, False)])
        put(-6, 0, 2, 19, bot + [word(0xdf, 7, False)])
        put(-6, 0, 3, 19, [word(0x5e, 7, False)] * 4 + [word(0x1b, 7, False)])
    if s & INTRO:
        name = rom_str(r, S_NAMES[h.race])
        line = rom_str(r, S_RACES + RACE_STRIDE * h.race)
        cs = _text(line, 4, True)
        at = line.index(name)
        cs[at:at + len(name)] = _text(name, 5, True)
        put(-6, 3, 8, 8, _text(rom_str(r, _S_BLANK26), 4, True))
        put(-6, 3, 9, 8, _text(rom_str(r, _S_TIME_TO if h.race < 2 else _S_EXTRA), 4, True))
        put(-6, 3, 10, 8, cs)
        put(-6, 3, 11, 8, _text(rom_str(r, _S_BLANK26), 4, True))
        top, bot = _big_digits("%2d" % h.add, 5, True)
        put(-6, 3, 9, 29, top)
        put(-6, 3, 10, 29, bot)
        if s & SILLY:
            for row, a in ((14, _S_BLANK31), (15, _S_WRONG), (16, _S_BLANK31)):
                put(-6, 3, row, 6, _text(rom_str(r, a), 5, True))
    if s & BONUS:
        put(-3, 0, 3, 3, _text(rom_str(r, _S_BONUS_FOR), 0, True))
        line = rom_str(r, _S_TIME_LEFT)
        v = _commas(h.value)
        put(-3, 0, 4, 3, _text(line[:len(line) - len(v)] + v, 0, True))
    if s & OUT:
        blank = " " * 16
        for row, t in ((24, blank), (25, rom_str(r, _S_OUT)), (26, blank), (27, rom_str(r, _S_OVER)),
                       (28, blank)):
            put(-6, 3, row, 13, _text(t, 0, True))
    if s & FIN:
        sec = min(h.seconds, 99)
        cong = BIG["CONGRATULATIONS!"]
        put(-6, 3, 2, 6, [word(c, 4, False) for c in cong[0]])
        put(-6, 3, 3, 6, [word(c, 4, False) for c in cong[1]])
        put(-6, 3, 7, 12, _text(rom_str(r, _S_YOU), 5, False))
        put(-6, 3, 8, 12, _text(rom_str(r, _S_THE), 5, False))
        fin = rom_str(r, _S_BONUS_FIN)
        label = fin.rstrip("0123456789,")
        put(-7, 3, 13, 9, _text(label.rstrip(), 5, False))
        put(-9, 3, 13, 9 + len(label), _text(fin[len(label):], 5, False))
        put(-7, 3, 15, 10, _text("%2d" % sec, 4, False))
        put(-7, 3, 15, 13, _text(rom_str(r, _S_SEC), 5, False))
        put(-9, 3, 15, 33, _text("%6s" % _commas(sec * 1000), 5, False))
        died = rom_str(r, _S_DIED)
        put(-7, 3, 17, 7, _text(died[:-1].rstrip(), 5, False))
        put(-7, 3, 17, 12, _text("%2d" % min(h.deaths, 99), 4, False))
        put(-9, 3, 17, 32, _text(died[-1:], 5, False))
        put(-9, 3, 17, 33, _text("%6s" % _commas(min(h.deaths, 20) * 1000), 5, False))
        put(-7, 3, 19, 23, _text(rom_str(r, _S_TOTAL), 5, False))
        put(-9, 3, 19, 32, _text("%7s" % _commas(h.value), 5, False))
        f = BIG["FINAL"]
        put(-4, 3, 22, 4, [word(c, 4, False) for c in f[0]])
        put(-4, 3, 23, 4, [word(c, 4, False) for c in f[1]])
        sc = BIG["SCORE:"]
        put(-10, 3, 24, 12, [word(c, 4, False) for c in sc[0]])
        put(-10, 3, 25, 12, [word(c, 4, False) for c in sc[1]])
        top, bot = _big_digits(_commas(h.score), 4)
        put(-10, 3, 24, 40 - len(top), top)
        put(-10, 3, 25, 40 - len(bot), bot)
    if s & _TITLE:
        out += title_items(h.table, r, min(h.stage, TITLE_STAGES - 1))
    if s & DEMO:
        out += _demo_items(h.banner, r)
    return out


RADIX = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:;<"  # initials (radix 40 word)

# --- title (Phase 6a, MARBLE.md "Attract"): arcade rows/cols/colours from MAME ----------
_S_TROUBLE, _S_COIN, _S_CREDITS, _S_COPYRIGHT = 0x235a8, 0x22f6c, 0x22ff8, 0x22f04


def _table_line(rank, initials, score):
    """16 cells from arcade column 13: rank right aligned in 3, initials, score in 9."""
    return "%3s %s%9s" % ("#%d" % rank, initials, "{:,}".format(score))


TITLE_STAGES = 3  # the title draws over 3 frames: table, copyright, coin line


def title_items(table, r, stage=TITLE_STAGES - 1):
    """(dx, dy, row, col, words) of the title, colour 5 / 4, transparent; stage = frames
    since the title started (0: header and table, 1: + copyright, 2: + coin line)."""
    out = []
    put = lambda dx, dy, row, col, ws: out.append((dx, dy, row, col, ws))
    put(-6, 3, 9, 15, _text(rom_str(r, _S_TROUBLE), 5, False))
    for k, (ini, sc) in enumerate(table):
        put(-6, 3, 11 + k, 13, _text(_table_line(k + 1, ini, sc), 4, False))
    if stage >= 1:
        put(-6, 3, 29, 12, _text(rom_str(r, _S_COPYRIGHT), 4, False))
    if stage >= 2:
        put(-2, 3, 28, 3, _text(rom_str(r, _S_COIN), 5, False))
        put(-7, 3, 28, 24, _text(rom_str(r, _S_CREDITS) + "0" + " " * 4, 5, False))
    return out


# --- attract demo (Phase 6b, MARBLE.md "Demo race"): coin box fitted to MAME, banners ----
BANNERS = 0x242aa   # long per banner: string chain (col, row, string, marker 2 = next, next)
BANNER_W = 13       # every banner line


def chain(r, a):
    """Alpha string chain -> [(row, col, text)] (render-string-chain, rotation 0)."""
    out = []
    while True:
        col, row = md.prog_bytes(r, a, 2)
        out.append((row, col, rom_str(r, md.u32(r, a + 2))))
        if md.prog_bytes(r, a + 6, 1)[0] <= 1:
            return out
        a = md.u32(r, a + 8)


def _banner_dx(col):
    """Banner move into the view: 6 columns left as the other banners, kept inside."""
    return max(0, min(col - 6, _VIEW_COLS - BANNER_W)) - col


def _demo_items(banner, r):
    """(dx, dy, row, col, words) of the demo: coin box (rows 27-29, colour 6 lines, colour 5
    and 4 opaque texts; the 36 arcade columns cut to 30), the banner (colour 5 opaque)."""
    out = []
    put = lambda dx, dy, row, col, ws: out.append((dx, dy, row, col, ws))
    line = word(0x5f, 6, False)
    put(-3, 3, 27, 3, [line] * 15)
    put(-5, 3, 27, 24, [line] * 15)
    put(-3, 3, 28, 3, _text(rom_str(r, _S_COIN), 5, True))
    put(-3, 3, 28, 18, [line] * 6)
    put(-5, 3, 28, 24, _text(rom_str(r, _S_CREDITS) + "0" + " " * 4, 5, True))
    put(-6, 3, 29, 12, _text(rom_str(r, _S_COPYRIGHT), 4, True))
    if banner != NO_BANNER:
        for row, col, t in chain(r, md.u32(r, BANNERS + 4 * banner)):
            put(_banner_dx(col), 3, row, col, _text(t, 5, True))
    return out


def view(h, r):
    """Remake grid: VIEW_ROWS x VIEW_COLS words, items moved by (dx, dy), clipped."""
    g = [[0] * _VIEW_COLS for _ in range(_VIEW_ROWS)]
    for dx, dy, row, col, ws in items(h, r):
        for k, w in enumerate(ws):
            y, x = row + dy, col + k + dx
            if 0 <= y < _VIEW_ROWS and 0 <= x < _VIEW_COLS:
                g[y][x] = w
    return g


def palette(h, r):
    """32 alpha palette words (IRGB) of state h."""
    if not h.screen & PAL:
        return [0] * 32
    pal = [md.u16(r, md.PAL_ALPHA + 2 * i) for i in range(32)]
    if h.screen & BONUS_PALETTE:
        for i, v in _BONUS_PAL.items():
            pal[i] = v
    if h.pal3 != 0xff:
        pal[3] = md.u16(r, PULSE + 2 * h.pal3)
    if h.pal11 != 0xff:
        pal[11] = md.u16(r, PULSE + 2 * h.pal11)
    return pal


STRINGS = MappingProxyType({   # remake string name -> ROM address
    "you": _S_YOU, "the": _S_THE, "bonus_fin": _S_BONUS_FIN, "sec": _S_SEC, "died": _S_DIED,
    "total": _S_TOTAL, "blank26": _S_BLANK26, "time_to": _S_TIME_TO, "extra": _S_EXTRA,
    "wrong": _S_WRONG, "blank31": _S_BLANK31, "bonus_for": _S_BONUS_FOR, "time_left": _S_TIME_LEFT,
    "out": _S_OUT, "over": _S_OVER, "trouble": _S_TROUBLE, "coin": _S_COIN,
    "credits": _S_CREDITS, "copyright": _S_COPYRIGHT,
})
_BORDER_CODES = (0x5f, 0x5e, 0xdf, 0xff, 0x1b)
CODES = 0x180                  # marble_hud_glyph entries (codes 0-0x17f)
NO_GLYPH = 0xff                # blank: code 0, empty glyphs


def glyph_codes(r):
    """Every alpha code a HUD item can show, ascending."""
    strs = [rom_str(r, a) for a in STRINGS.values()]
    strs += [rom_str(r, S_RACES + RACE_STRIDE * k) for k in range(len(S_NAMES))]
    strs += [t for b in range(7) for _, _, t in chain(r, md.u32(r, BANNERS + 4 * b))]
    codes = {ord(ch) for t in strs + ["0123456789,-SCORE#", RADIX[1:27]] for ch in t if ch != " "}
    codes |= set(_BORDER_CODES)
    codes |= {0x100 + k for k in range(40)} | {_BLANK, _COMMA_LO, _COLON}
    codes |= {c for top, bot in BIG.values() for c in top + bot}
    return sorted(codes)


def glyph_rows(glyph):
    """8 rows, 2 bpp, left pixel in bits 15-14 (marble_hud_glyphs)."""
    return [sum(p << (14 - 2 * x) for x, p in enumerate(line)) for line in glyph]


# remake layout cases (host test fixture): screen, score, value, seconds, race, add, deaths,
# pal3, pal11 (marble_video::Hud field order)
LAYOUT_CASES = (
    (PAL | TOP, 1020, 0, 50, 0, 0, 0, 0xff, 0xff),
    (PAL | TOP, 9999999, 0, 5, 0, 0, 0, 0xff, 0xff),       # 7 digit score, leading zero
    (PAL | TOP, 0, 0, 150, 0, 0, 0, 0xff, 3),               # clamp 99, timer flash
    (PAL | TOP, 0, 0, -1, 0, 0, 0, 0xff, 0xff),             # clamp 0
    (PAL | TOP | INTRO, 0, 0, 0, 0, 60, 0, 0xff, 0xff),
    (PAL | TOP | INTRO, 0, 0, 55, 1, 5, 0, 0xff, 0xff),    # one digit: leading space
    (PAL | TOP | INTRO, 6020, 0, 84, 3, 30, 0, 0xff, 0xff),   # EXTRA TIME FOR
    (PAL | TOP | INTRO | SILLY, 6020, 0, 78, 4, 20, 0, 0xff, 0xff),
    (PAL | TOP | BONUS | BONUS_PALETTE, 1020, 5000, 50, 1, 0, 0, 7, 0xff),
    (PAL | TOP | OUT, 140, 0, 0, 0, 0, 0, 0xff, 0xff),
    (PAL | FIN, 46920, 88750, 69, 6, 0, 0, 0xff, 0xff),
    (PAL | FIN, 9999999, 0, 150, 6, 0, 25, 0xff, 0xff),    # clamps: 99 s, 20 deaths
    (0, 1020, 0, 50, 0, 0, 0, 0xff, 0xff),                  # palette off: nothing shown
    (PAL | _TITLE, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 0),      # title first frame: table only
    (PAL | _TITLE, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 1, 0),      # + copyright
    (PAL | _TITLE, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 2, 0),
    (PAL | _TITLE, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 9, 1),      # 7 digit score, blank initials
    (PAL | TOP | DEMO, 1330, 0, 45, 0, 0, 0, 0xff, 0xff, 0, 0, NO_BANNER),  # coin box
    (PAL | TOP | DEMO, 0, 0, 60, 0, 0, 0, 0xff, 0xff, 0, 0, 0),   # banner clipped right
    (PAL | TOP | DEMO, 80, 0, 58, 0, 0, 0, 0xff, 0xff, 0, 0, 1),  # clipped left
    (PAL | TOP | DEMO, 300, 0, 46, 0, 0, 0, 0xff, 0xff, 0, 0, 3),  # 5 lines
    (PAL | TOP | DEMO, 300, 0, 46, 1, 0, 0, 0xff, 0xff, 0, 0, 6),
)
LAYOUT_FIELDS = ("screen", "score", "value", "seconds", "race", "add", "deaths", "pal3", "pal11",
                 "stage", "tcase", "banner")
_LAYOUT_DEFAULTS = (0, 0, NO_BANNER)  # stage, tcase, banner
_BIG_ENTRY = ("   ", 9999999)


def _layout_table(r, tcase):
    from romconv.games.marble.sim import marble_attract
    t = marble_attract.defaults(r)
    return [_BIG_ENTRY] + t[1:] if tcase else t


def layout_states(r):
    out = []
    for c in LAYOUT_CASES:
        c = tuple(c) + _LAYOUT_DEFAULTS[len(c) - len(LAYOUT_FIELDS) + len(_LAYOUT_DEFAULTS):]
        h = Hud(**dict(zip(LAYOUT_FIELDS, c)))
        if h.screen & _TITLE:
            h.table = _layout_table(r, h.tcase)
        out.append(h)
    return out
