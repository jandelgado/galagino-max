"""Marble Madness jingle and effects for the C64 player (MARBLE.md "Sound"), written by ear
from MAME recordings of the arcade: an interpretation, not a capture. sid.py turns each list
into a player track and writes INSTRUMENTS into the instrument slots the music doesn't use.

Note (start, length, MIDI note, instrument): start and length in 60 Hz frames, the gate stays
on for length frames and the next note may start one frame later. Notes of a list never
overlap. An effect plays on SID voice 0, a second list on voice 1; the jingle is a melody
plus sustained chord notes.

The arcade's YM2151 tones are soft (energy below 1 kHz) and shaped: they swell, fade, or come
as short thuds. Triangle and noise instruments with matching envelopes stand in for them; the
SID filter stays with the music.
"""
from types import MappingProxyType

_TOP_NOTE = 95  # the player's note table ends here
_TRI, _PULSE, _NOISE = 0x10, 0x40, 0x80
_SQUARE, _BROAD, _NARROW, _SLIM = 0x800, 0x700, 0x400, 0x300   # pulse widths: 50, 44, 25, 19 %

# SID envelope nibbles: attack 0 = 2 ms, 9 = 250 ms, 10 = 500 ms; decay / release 0 = 6 ms,
# 2 = 48 ms, 6 = 204 ms, 9 = 750 ms, 10 = 1.5 s, 11 = 2.4 s, 12 = 3 s; sustain 15 = full,
# 10 = -3.5 dB, 6 = -8 dB, 4 = -11.5 dB, 2 = -17.5 dB, 1 = -23.5 dB
_PLUCK, _GLIDE, _FADE, _DRONE, _BUZZ, _THUD, _STRIKE = range(15, 22)
_LEVELS = (15, 10, 6, 2, 1)
_BRIGHT_LEVELS = (15, 6, 2)
# steady notes at a level: an effect fading over many notes steps down this ladder
_TONE = MappingProxyType({lv: 22 + i for i, lv in enumerate(_LEVELS)})
_BRIGHT = MappingProxyType({lv: 27 + i for i, lv in enumerate(_BRIGHT_LEVELS)})
_SWELL = 31
# soft notes: the gate closes while the attack still rises, so attack and gate set the level
# without the ladder's spike (every attack first reaches full level, ~-11 dB on 3-frame notes)
# attack nibble: slot, levels for a 1 / 4 frame gate (dB)
_SOFT_TRI = MappingProxyType({5: 1, 7: 2, 8: 3, 9: 4, 10: 7})     # -10 -14 -16 -24 -30 @ 1 frame
_SOFT_BUZZ = MappingProxyType({2: 8, 8: 9, 9: 10, 12: 12})        # 0 -4 -11 -24 @ 4 frames

INSTRUMENTS = MappingProxyType({          # slot: (waveform, AD, SR, pulse width)
    _PLUCK: (_TRI, 0x06, 0xA4, _SQUARE),  # soft tone, settles 3 dB down
    _GLIDE: (_TRI, 0x20, 0xF9, _SQUARE),  # steady; a new note barely restarts (slow release)
    _FADE: (_TRI, 0x0A, 0x05, _SQUARE),   # struck, fades out over 1.5 s
    _DRONE: (_TRI, 0x0B, 0x09, _SQUARE),  # held low note fading over 2.4 s
    _STRIKE: (_TRI, 0x04, 0x24, _SQUARE),  # falls 17 dB in 100 ms
    _SWELL: (_TRI, 0xA0, 0xF6, _SQUARE),   # 500 ms in, then steady
    _BUZZ: (_PULSE, 0x2B, 0x05, _SLIM),     # rich low buzz, fades over 2.4 s
    _THUD: (_NOISE, 0x00, 0xC2, _SQUARE),   # noise burst, cut off
    **{_TONE[lv]: (_TRI, 0x00, lv << 4 | 4, _SQUARE) for lv in _LEVELS},
    **{_BRIGHT[lv]: (_PULSE, 0x00, lv << 4 | 4, _NARROW) for lv in _BRIGHT_LEVELS},
    **{slot: (_TRI, a << 4, 0xF0, _SQUARE) for a, slot in _SOFT_TRI.items()},
    **{slot: (_PULSE, a << 4, 0xF0, _BROAD) for a, slot in _SOFT_BUZZ.items()},
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


def _run(notes, frames):
    """every note of notes for frames each, as _seq() parts"""
    return [(n, frames) for n in notes]


def _hit(note, gate, frames, inst=None):
    """a struck note: gate frames on, then released until frames are over"""
    head = (note, gate + 1) if inst is None else (note, gate + 1, inst)
    return [head, (None, frames - gate - 1)]


def _soft(note, gate, frames, inst):
    """a soft note sounding gate frames (the player closes the gate 2 frames before the next
    event), then silent until frames are over"""
    return [(note, gate + 2, inst)] + ([(None, frames - gate - 2)] if gate + 2 < frames else [])


def _fall(top, delay):
    """the fall from top: swells in after delay frames, then steps down a semitone at a time"""
    return _seq((None, delay), (top, 30 - delay, _SWELL), (top - 1, 6),
                *zip(range(top - 2, top - 9, -1), (18, 24, 14, 12, 10, 10, 22)), inst=_GLIDE)


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

_EFFECTS = MappingProxyType({
    # goal: up to a held C#5, then a quiet low C#3 fading out
    # goal: up to C#5, struck four times, then held; over a C#3 drone
    0x2F: (_seq(*_run([61, 66, 71], 6), *[(73, 6, _STRIKE)] * 4, (73, 18, _TONE[15]), inst=_PLUCK),
           _seq((49, 120), inst=_DRONE)),
    0x30: _seq((63, 3), (68, 8), (71, 7), (73, 14), (71, 7), (68, 8), (73, 7), (71, 8), (68, 14),
               inst=_TONE[15]),
    # jump: D5 and its octave swell in; D5 dips and comes back
    0x31: (_seq((74, 98), (None, 4), (74, 42), inst=_SWELL),
           _seq((None, 18), (86, 126), inst=_SWELL)),
    0x32: _seq((17, 66), inst=_BUZZ),                                      # steelie hit
    0x33: _seq(*_hit(30, 16, 33), *_hit(30, 16, 21), *_hit(30, 14, 15), inst=_THUD),  # caught
    # players hit: a low wide buzz struck every 6 frames, brighter / darker in turns (arcade:
    # rough FM tones repeating every 24 ms), 5 dB quieter every second strike
    0x37: _seq(*[p for k, (a, gate) in enumerate(((2, 4), (2, 4), (8, 4), (8, 4), (9, 4), (9, 4),
                                                  (9, 3), (9, 3), (9, 2), (9, 2), (12, 4), (9, 1),
                                                  (12, 2), (12, 3), (12, 1), (12, 2)))
                 for p in _soft((28, 26)[k % 2], gate, 6, _SOFT_BUZZ[a])], inst=_PLUCK),
    # break: a falling figure, bright, fading over nine rounds
    0x38: _seq(*[p for inst in (_BRIGHT[15], _BRIGHT[6], _BRIGHT[6], _BRIGHT[2], _BRIGHT[2],
                                _TONE[1], _TONE[1], _TONE[1], _TONE[1])
                 for p in ((87, 3, inst), *_run([86, 85, 83], 3))], inst=_PLUCK),
    0x39: _seq(*[(n + d, 3) for d in (0, -2, 1, -3, 2, -1, 0, -2)        # dizzy: warble
                 for n in (93, 89, 85, 82, 85, 89)], inst=_PLUCK),
    # lost (embedded): whole tone runs down, the second from higher up, then a C4 fading out
    0x3C: _seq(*_run(range(80, 64, -2), 3), *_run(range(74, 60, -2), 3), (60, 62, _FADE),
               inst=_PLUCK),
    0x3D: _seq(*_run(range(92, 64, -2), 3), *_run(range(74, 60, -2), 3), (60, 62, _FADE),
               inst=_PLUCK),
    0x3E: _seq(*_run(range(60, 76, 2), 3), *_run(range(66, 80, 2), 3), (80, 40, _FADE),  # carried
               inst=_PLUCK),
    # bounce: a dark cluster (arcade: FM partials 49 to 63), as a fast arpeggio fading out
    0x42: _seq(*[(n, 3, inst) for n, inst in
                 zip([63, 58, 54, 49] * 7,
                     [inst for inst in (_TONE[15], _TONE[15], _TONE[10], _TONE[10], _TONE[6],
                                        *(_SOFT_TRI[a] for a in (5, 7, 7, 8, 8, 8, 9, 9, 10)))
                      for _ in range(2)])], inst=_PLUCK),
    # fall: swells in, then steps down; the octave above swells in later
    0x45: (_fall(83, 4), _fall(95, 10)),
    # time bonus: a bell, then a rise to its octave (arcade a tone higher: 97 is off the table)
    0x53: _seq((83, 98), (88, 5), (90, 5), (92, 6), (95, 34), inst=_FADE),
})
# effects with vibrato (arcade jump and fall: 5 Hz, +-0.3 semitone)
VIBRATO = frozenset({0x31, 0x45})
# command -> tracks: one on voice 0, a second one takes voice 1 from the music
EFFECTS = MappingProxyType({c: v if isinstance(v, tuple) else (v,) for c, v in _EFFECTS.items()})


def check():
    """every list monophonic, notes in the player's range"""
    for ns in [t for tracks in (*EFFECTS.values(), *JINGLES.values()) for t in tracks]:
        for (s, l, n, _), nxt in zip(ns, ns[1:] + [None]):
            assert l > 0 and 0 <= n <= _TOP_NOTE and (nxt is None or s + l < nxt[0]), (s, n)
