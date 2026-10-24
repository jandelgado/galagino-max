"""Marble Madness music (MARBLE.md "Sound"): the C64 tune's own songs for the race music, the
jingle written by ear in sfx.py as a song of its player (Marble_Madness.sid, format in
MARBLE.md) and a silent song for STOP. The effects come from the arcade's sound program
(marble_soundboard). Timing: tempo 0x3840, duration index k = 2^(7-k) frames, every length a
chain of events (frame exact).
"""
import struct
from types import MappingProxyType

from romconv.core.errors import ConversionError
from romconv.games.marble import sfx

_LOAD = 0x2700
_SONG_TABLE, _SONGS_MAX = 0x3dc0, 32
_TEMPO = 0x3840
# instrument: pulse width lo / hi, SR, AD, filter cutoff lo / hi, filter, waveform (0x3cdb)
_INSTRUMENTS, _INSTRUMENT_SIZE = 0x397a, 8
_SLOTS = 32
# the only instruments the tune's songs select (0x81 events; a register trace agrees): the
# other slots are free for the jingle
_MUSIC_INSTRUMENTS = frozenset({0, 5, 6, 11})
_MUSIC = MappingProxyType({0x08: 1, 0x0A: 2, 0x0C: 3, 0x0E: 4, 0x14: 5, 0x18: 6})
_STOP_CMDS = (0x00, 0x61)
# command kinds: to the sound board, a song (C64 tune only), stop (silent song + board)
_BOARD, _SONG, _STOP = 0, 1, 2


def _chain(n):
    """n frames as duration indices, largest first."""
    out = []
    for k in range(8):
        f = 1 << (7 - k)
        while n >= f:
            out.append(k)
            n -= f
    return out


def _track(notes):
    """notes [(start, length, midi, instrument)], monophonic: -> event bytes."""
    ev, t, cur = [], 0, None
    for s, l, m, inst in notes:
        if inst != cur:
            ev += [0x81, inst]
            cur = inst
        if s > t:
            for k in _chain(s - t):
                ev += [0x80, k]
            t = s
        ks = _chain(l + 1)                       # gate = total - 1 frame
        for i, k in enumerate(ks):
            ev += [m, k | (0x40 if i < len(ks) - 1 else 0)]
        t += l + 1
    return ev + [0xFF, 0xFF]


def _descriptor(ident, tracks, mask, priority):
    assert len(ident) == 4, ident
    return bytearray(ident) + bytes([_TEMPO & 0xFF, _TEMPO >> 8, 0x7F, len(tracks), mask, priority])


class _Tune:
    def __init__(self, data):
        self.hdr = bytearray(data[:0x7C])
        if data[0x7C] | data[0x7D] << 8 != _LOAD:
            raise ConversionError("marble: Marble_Madness.sid load address is not 0x2700")
        self.pay = bytearray(data[0x7E:])
        self.songs = struct.unpack(">H", self.hdr[0x0E:0x10])[0]

    def end(self):
        return _LOAD + len(self.pay)

    def put(self, addr, data):
        end = addr - _LOAD + len(data)
        if end > len(self.pay):
            self.pay.extend(b"\0" * (end - len(self.pay)))
        self.pay[addr - _LOAD:end] = data

    def piece(self, ident, tracks, mask, priority):
        """append descriptor + tracks; -> (descriptor address, track addresses, lengths)"""
        base = self.end()
        desc = _descriptor(ident, tracks, mask, priority)
        pos = base + len(desc) + 4 * len(tracks)
        ptrs, lens, blob = [], [], bytearray()
        for t in tracks:
            ptrs.append(pos + len(blob))
            lens.append(len(t))
            blob += bytes(t)
        for p in ptrs:
            desc += struct.pack("<H", p)
        for n in lens:
            desc += struct.pack("<H", n)
        self.put(base, bytes(desc + blob))
        return base, ptrs, lens

    def instrument(self, slot, data):
        """fill an instrument slot the music doesn't use"""
        if slot in _MUSIC_INSTRUMENTS or not 0 <= slot < _SLOTS:
            raise ConversionError(f"marble: C64 instrument {slot} is in use")
        self.put(_INSTRUMENTS + _INSTRUMENT_SIZE * slot, data)

    def song(self, ident, tracks, mask=0x07, priority=0x80):
        if self.songs == _SONGS_MAX:
            raise ConversionError("marble: C64 song table full")
        base, _, _ = self.piece(ident, tracks, mask, priority)
        self.put(_SONG_TABLE + 2 * self.songs, struct.pack("<H", base))
        self.songs += 1
        return self.songs

    def file(self):
        hdr = bytearray(self.hdr)
        hdr[0x0E:0x10] = struct.pack(">H", self.songs)
        end_page = hdr[0x78] + hdr[0x79]
        hdr[0x78] = (self.end() + 0x400 + 0xFF) >> 8          # behind data + alias page
        hdr[0x79] = end_page - hdr[0x78]
        return bytes(hdr) + struct.pack("<H", _LOAD) + bytes(self.pay)


def build(sid_bytes):
    """-> (file bytes, command table [(kind, arg)] * 128, silent song, alias page, ram size,
    song by command)"""
    tune = _Tune(sid_bytes)
    for slot, (wave, ad, sr, pw) in sfx.INSTRUMENTS.items():
        tune.instrument(slot, bytes([pw & 0xFF, pw >> 8, sr, ad, 0, 0, 0, wave]))
    table = [(_BOARD, 0)] * 128
    for c, s in _MUSIC.items():
        table[c] = (_SONG, s)
    silent = tune.song(b"STOP", [[0xFF, 0xFF]], mask=0x06)
    for c in _STOP_CMDS:
        table[c] = (_STOP, silent)
    songs = {}
    for c, tracks in sfx.JINGLES.items():
        songs[c] = tune.song(b"JI%02X" % c, [_track(ns) for ns in tracks])
        table[c] = (_SONG, songs[c])
    alias = (tune.end() + 0xFF) & ~0xFF
    ram = alias + 0x400 - _LOAD
    return tune.file(), table, silent, alias, ram, songs


def selfcheck():
    assert _chain(5) == [5, 7] and _chain(128) == [0] and _chain(255) == list(range(8))
    assert _track([(2, 3, 60, 11)]) == [0x81, 11, 0x80, 6, 60, 5 | 0, 0xFF, 0xFF]
    sfx.check()
