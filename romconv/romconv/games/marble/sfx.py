"""Marble Madness time out jingle for the C64 player (MARBLE.md "Sound"), written by ear from
a MAME recording of the arcade: an interpretation, not a capture. sid.py turns each list into
a player track and writes INSTRUMENTS into the instrument slots the music doesn't use.

Note (start, length, MIDI note, instrument): start and length in 60 Hz frames, the gate stays
on for length frames and the next note may start one frame later. Notes of a list never
overlap. The jingle is a melody plus sustained chord notes.

The arcade's YM2151 tones are soft (energy below 1 kHz) and shaped: they swell and fade.
Triangle instruments with matching envelopes stand in for them; the SID filter stays with the
music.
"""
from types import MappingProxyType

_TOP_NOTE = 95  # the player's note table ends here
_TRI = 0x10
_SQUARE = 0x800   # pulse width 50 % (unused by the triangle)

# SID envelope nibbles: attack 0 = 2 ms; decay / release 4 = 114 ms, 6 = 204 ms, 10 = 1.5 s;
# sustain 15 = full, 10 = -3.5 dB, 6 = -8 dB, 2 = -17.5 dB, 1 = -23.5 dB
_PLUCK, _FADE = 15, 17
_LEVELS = (15, 10, 6, 2, 1)
# steady notes at a level: the jingle fading over many notes steps down this ladder
_TONE = MappingProxyType({lv: 22 + i for i, lv in enumerate(_LEVELS)})

INSTRUMENTS = MappingProxyType({          # slot: (waveform, AD, SR, pulse width)
    _PLUCK: (_TRI, 0x06, 0xA4, _SQUARE),  # soft tone, settles 3 dB down
    _FADE: (_TRI, 0x0A, 0x05, _SQUARE),   # struck, fades out over 1.5 s
    **{_TONE[lv]: (_TRI, 0x00, lv << 4 | 4, _SQUARE) for lv in _LEVELS},
})


def _seq(*parts, inst):
    """parts (note, frames) or (note, frames, instrument from here on) back to back -> notes;
    note None is a rest"""
    out, t = [], 0
    for part in parts:
        n, f, inst = part if len(part) == 3 else (*part, inst)
        if n is not None:
            out.append((t, f - 1, n, inst))
        t += f
    return out


def _jingle_melody():
    """rising figure b, b+5, b+10, b+8 / b+5, b+10, b+15, b+14, an octave per two cells, then
    the top four notes falling over and over; swells over the first 12 notes, then fades"""
    notes, b = [], 41
    while b < 89:
        notes += [b, b + 5, b + 10, b + 8, b + 5, b + 10, b + 15, b + 14]
        b += 12
    top = [94, 92, 89, 87] * 5
    levels = [1, 1, 2, 2, 6, 6, 10, 10] + [15] * 6 + [10] * 6 + [6] * 12 + [2] * 16
    levels += [1] * (len(notes) + len(top) - len(levels))
    frames = [5] * len(notes) + [6] * len(top)
    return _seq((None, 2), *[(n, f, _TONE[lv]) for n, f, lv in zip(notes + top, frames, levels)],
                inst=_PLUCK)


# time out: the melody, a struck Bb2 / D3 chord at its loudest point
JINGLES = MappingProxyType({
    0x3F: (_jingle_melody(), _seq((None, 45), (46, 80), inst=_FADE),
           _seq((None, 45), (50, 80), inst=_FADE)),
})


def check():
    """every list monophonic, notes in the player's range"""
    for ns in [t for tracks in JINGLES.values() for t in tracks]:
        for (s, l, n, _), nxt in zip(ns, ns[1:] + [None]):
            assert l > 0 and 0 <= n <= _TOP_NOTE and (nxt is None or s + l < nxt[0]), (s, n)
