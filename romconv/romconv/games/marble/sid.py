"""Marble Madness sound (MARBLE.md "Sound"): MAME captures of the arcade's YM2151 / POKEY
writes per command (tools/mame_sound.lua) become songs of the C64
tune's own player (Marble_Madness.sid, format in MARBLE.md): jingles as songs that replace
the music, effects as one track songs on slot 1 / SID voice 0 started by a 6502 stub over the
music, a silent song for STOP. Timing: tempo 0x3840, duration index k = 2^(7-k) frames,
every length a chain of events (frame exact).

The capture is not distributed: tools/marble_sid_extract.py reduces it to the note lists in
marble_sound_notes.json (no ROM or register data), which build() reads.
"""
import json
import struct
from pathlib import Path
from types import MappingProxyType

from romconv.core.errors import ConversionError

_NOTES_FILE = "marble_sound_notes.json"

_LOAD = 0x2700
_SONG_TABLE, _SONGS_MAX = 0x3dc0, 32
_TEMPO = 0x3840
_SLOT_START = 0x316b
_NOTE = MappingProxyType({0: 0, 1: 1, 2: 2, 4: 3, 5: 4, 6: 5, 8: 6, 9: 7, 10: 8, 12: 9, 13: 10, 14: 11})

_MUSIC = MappingProxyType({0x08: 1, 0x0A: 2, 0x0C: 3, 0x0E: 4, 0x14: 5, 0x18: 6})
_STOP = (0x00, 0x61)
# jingles: command -> (melody YM channels, sustain YM channels, melody / sustain instrument)
JINGLES = MappingProxyType({0x3F: ((0, 1), (6, 5), 11, 6)})
# effects: command -> instrument; noise (YM channel 7 noise or POKEY) uses 12
EFFECTS = MappingProxyType({0x2F: 11, 0x30: 11, 0x31: 11, 0x32: 11, 0x33: 11, 0x37: 11, 0x38: 12, 0x39: 11,
                            0x3C: 11, 0x3D: 11, 0x3E: 11, 0x42: 0, 0x45: 12, 0x53: 11, 0x5E: 12, 0x5F: 12})
_IGNORE, _SONG, _EFFECT = 0, 1, 2


def midi(kc, kf):
    """YM2151 KC/KF to MIDI (KC 0x4a = A4 = 69)."""
    n = _NOTE.get(kc & 15, _NOTE.get((kc & 15) - 1, 0))
    return ((kc >> 4) & 7) * 12 + n + 13 + kf / 64


def _chain(n):
    """n frames as duration indices, largest first."""
    out = []
    for k in range(8):
        f = 1 << (7 - k)
        while n >= f:
            out.append(k)
            n -= f
    return out


def fit(m, top):
    n = round(m)
    while n > top:
        n -= 12
    return max(0, n)


def _track(notes, inst):
    """notes [(start, length, midi)], monophonic: -> event bytes."""
    ev, t = [0x81, inst], 0
    for s, l, m in notes:
        if s > t:
            for k in _chain(s - t):
                ev += [0x80, k]
            t = s
        ks = _chain(l + 1)                       # gate = total - 1 frame
        for i, k in enumerate(ks):
            ev += [m, k | (0x40 if i < len(ks) - 1 else 0)]
        t += l + 1
    return ev + [0xFF, 0xFF]


def mono(notes):
    """cut each note one frame before the next one starts"""
    out = []
    for i, (s, l, m) in enumerate(notes):
        nxt = notes[i + 1][0] if i + 1 < len(notes) else None
        if nxt is not None:
            l = min(l, nxt - s - 1)
        if l > 0:
            out.append((s, l, m))
    return out


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

    def w(self, addr):
        return self.pay[addr - _LOAD] | self.pay[addr - _LOAD + 1] << 8

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

    def song(self, ident, tracks, mask=0x07, priority=0x80):
        if self.songs == _SONGS_MAX:
            raise ConversionError("marble: C64 song table full")
        base, _, _ = self.piece(ident, tracks, mask, priority)
        self.put(_SONG_TABLE + 2 * self.songs, struct.pack("<H", base))
        self.songs += 1
        return self.songs

    def stub(self, fx):
        """6502 effect entry, A = effect index: slot 1 = one track on voice 0, priority 0xc0,
        after the virtual voices of slot 0, then 0x316b(y = 1). fx: [(track, length)]."""
        table = self.end()
        self.put(table, b"".join(struct.pack("<HH", p, n) for p, n in fx))
        s = self.end()
        lo, hi = table & 0xFF, table >> 8
        code = bytes([
            0x0A, 0x0A, 0xAA,              # asl; asl; tax       x = 4 * index
            0xAD, 0x34, 0x27,              # lda $2734           slot 0 first voice
            0x18, 0x6D, 0x2C, 0x27,        # clc; adc $272c      + slot 0 tracks = v
            0x8D, 0x35, 0x27,              # sta $2735           slot 1 first voice
            0xA8, 0xC8, 0x8C, 0x27, 0x27,  # tay; iny; sty $2727 voices in use = v + 1
            0x0A, 0xA8,                    # asl; tay            y = 2v
            0xBD, lo, hi, 0x99, 0x68, 0x27,                    # lda table,x; sta $2768,y
            0xBD, (table + 1) & 0xFF, (table + 1) >> 8, 0x99, 0x69, 0x27,
            0xBD, (table + 2) & 0xFF, (table + 2) >> 8, 0x99, 0x78, 0x27,
            0xBD, (table + 3) & 0xFF, (table + 3) >> 8, 0x99, 0x79, 0x27,
            0xA9, _TEMPO & 0xFF, 0x8D, 0x32, 0x27,              # slot 1 tempo
            0xA9, _TEMPO >> 8, 0x8D, 0x33, 0x27,
            0xA9, 0x7F, 0x8D, 0x2F, 0x27,                      # volume
            0xA9, 0x01, 0x8D, 0x2D, 0x27, 0x8D, 0x3B, 0x27,    # 1 track, voice mask voice 0
            0xA9, 0xC0, 0x8D, 0x3D, 0x27,                      # priority over the music
            0xA9, 0x02, 0x8D, 0x28, 0x27,                      # two slots
            0xA0, 0x01, 0x4C, _SLOT_START & 0xFF, _SLOT_START >> 8,  # ldy #1; jmp $316b
        ])
        self.put(s, code)
        return s

    def file(self):
        hdr = bytearray(self.hdr)
        hdr[0x0E:0x10] = struct.pack(">H", self.songs)
        end_page = hdr[0x78] + hdr[0x79]
        hdr[0x78] = (self.end() + 0x400 + 0xFF) >> 8          # behind data + alias page
        hdr[0x79] = end_page - hdr[0x78]
        return bytes(hdr) + struct.pack("<H", _LOAD) + bytes(self.pay)


def load_notes():
    with open(Path(__file__).with_name(_NOTES_FILE)) as f:
        return json.load(f)


def build(sid_bytes, notes, log):
    """notes: load_notes() -> (file bytes, command table [(kind, arg)] * 128, silent song,
    stub address, alias page, ram size, effect index by command, song by command)"""
    tune = _Tune(sid_bytes)
    table = [(_IGNORE, 0)] * 128
    for c, s in _MUSIC.items():
        table[c] = (_SONG, s)
    silent = tune.song(b"STOP", [[0xFF, 0xFF]], mask=0x06)
    for c in _STOP:
        table[c] = (_SONG, silent)
    songs = {}
    for c, (mel, sus, im, isus) in JINGLES.items():
        mel_notes, *sus_notes = notes["jingles"]["%02x" % c]
        songs[c] = tune.song(b"JI%02X" % c, [_track(mel_notes, im)] + [_track(n, isus) for n in sus_notes])
        table[c] = (_SONG, songs[c])
    fx, index = [], {}
    for c, inst in EFFECTS.items():
        ns = notes["effects"]["%02x" % c]
        if not ns:
            log.info(f"marble sound: 0x{c:02x} has no output, ignored")
            continue
        _, ptrs, lens = tune.piece(b"FX%02X" % c, [_track(ns, inst)], 0x01, 0xC0)
        index[c] = len(fx)
        table[c] = (_EFFECT, len(fx))
        fx.append((ptrs[0], lens[0]))
    stub = tune.stub(fx)
    alias = (tune.end() + 0xFF) & ~0xFF
    ram = alias + 0x400 - _LOAD
    return tune.file(), table, silent, stub, alias, ram, index, songs


def selfcheck():
    assert abs(midi(0x4A, 0) - 69) < 1e-9 and abs(midi(0x4E, 0) - 72) < 1e-9
    assert _chain(5) == [5, 7] and _chain(128) == [0] and _chain(255) == list(range(8))
    assert fit(107, 95) == 95 and fit(108, 95) == 84
    assert _track([(2, 3, 60)], 11) == [0x81, 11, 0x80, 6, 60, 5 | 0, 0xFF, 0xFF]
    assert mono([(0, 10, 60), (5, 3, 62)]) == [(0, 4, 60), (5, 3, 62)]
