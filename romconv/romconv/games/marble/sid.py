"""Marble Madness sound (MARBLE.md "Sound"): the jingle and effects written by ear in sfx.py
become songs of the C64 tune's own player (Marble_Madness.sid, format in MARBLE.md): jingles
as songs that replace the music, effects as songs on slot 1 / SID voice 0 (+ 1) started by
a 6502 stub over the music, a silent song for STOP. Timing: tempo 0x3840, duration index
k = 2^(7-k) frames, every length a chain of events (frame exact).
"""
import struct
from types import MappingProxyType

from romconv.core.errors import ConversionError
from romconv.games.marble import sfx

_LOAD = 0x2700
_SONG_TABLE, _SONGS_MAX = 0x3dc0, 32
_TEMPO = 0x3840
_SLOT_START = 0x316b
# instrument: pulse width lo / hi, SR, AD, filter cutoff lo / hi, filter, waveform (0x3cdb)
_INSTRUMENTS, _INSTRUMENT_SIZE = 0x397a, 8
_SLOTS = 32
# the only instruments the tune's songs select (0x81 events; a register trace agrees): the
# other slots are free for the effects
_MUSIC_INSTRUMENTS = frozenset({0, 5, 6, 11})
_FX_TRACKS = 2  # an effect plays on voice 0, a second track takes voice 1 from the music
_FX_PRIORITY = 0xC0
# vibrato hook: the play routine's register copy (jsr $3c02 at 0x3e4b) is redirected to code
# that copies, then offsets the SID frequency of the voices the effect owns (priority of the
# voice's last note-on 0x3977 + v, register offset 0x3a86 + v, register shadow 0x38df)
_PLAY_COPY, _COPY = 0x3E4B, 0x3C02
_OWNER, _REG, _FREQ_LO, _FREQ_HI = 0x3977, 0x3A86, 0x38DF, 0x38E0
# offset per frame in 1/256 of the frequency: 12 frames = 5 Hz, +-5 = +-0.34 semitone
_LFO = (0, 3, 4, 5, 4, 3, 0, -3, -4, -5, -4, -3)

_MUSIC = MappingProxyType({0x08: 1, 0x0A: 2, 0x0C: 3, 0x0E: 4, 0x14: 5, 0x18: 6})
_STOP = (0x00, 0x61)
_IGNORE, _SONG, _EFFECT = 0, 1, 2


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


def _word(addr):
    return [addr & 0xFF, addr >> 8]


def _asm(base, items):
    """6502 bytes at base; "name:" defines a label, "name" is a branch to it (1 byte)"""
    labels, pc = {}, base
    for x in items:
        if isinstance(x, str) and x.endswith(":"):
            labels[x[:-1]] = pc
        else:
            pc += 1
    out = bytearray()
    for x in items:
        if not isinstance(x, str):
            out.append(x)
        elif not x.endswith(":"):
            rel = labels[x] - (base + len(out) + 1)
            assert -128 <= rel < 128, x
            out.append(rel & 0xFF)
    return bytes(out)


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

    def vibrato(self):
        """install the vibrato hook (see _PLAY_COPY); -> address of its state: on flag, phase"""
        if self.pay[_PLAY_COPY - _LOAD:_PLAY_COPY - _LOAD + 3] != bytes([0x20, *_word(_COPY)]):
            raise ConversionError("marble: Marble_Madness.sid play routine differs")
        state = self.end()
        on, phase, lfo, count, lo, hi = (state + k for k in range(6))
        table = state + 6
        code = _asm(table + len(_LFO), [
            0x20, *_word(_COPY),                            # jsr copy        registers to SID
            0xAD, *_word(on), 0xF0, "done",                 # lda on; beq done
            0xAC, *_word(phase), 0xC8, 0xC0, len(_LFO), 0x90, 2, 0xA0, 0,  # phase + 1 mod 12
            0x8C, *_word(phase), 0xB9, *_word(table), 0x8D, *_word(lfo), 0xF0, "done",
            0xA2, 0,                                        # ldx #0          voice
            "loop:", 0xBD, *_word(_OWNER), 0xC9, _FX_PRIORITY, 0xD0, "next",  # effect's?
            0xBC, *_word(_REG),                             # ldy reg,x       register offset
            0xA9, 0, 0x8D, *_word(lo), 0x8D, *_word(hi),    # offset = |lfo| * freq hi
            0xAD, *_word(lfo), 0x10, "pos", 0x49, 0xFF, 0x18, 0x69, 1,
            "pos:", 0x8D, *_word(count),
            "add:", 0xAD, *_word(lo), 0x18, 0x79, *_word(_FREQ_HI), 0x8D, *_word(lo),
            0x90, 3, 0xEE, *_word(hi), 0xCE, *_word(count), 0xD0, "add",
            0xAD, *_word(lfo), 0x10, "set",                 # lfo < 0: offset = -offset
            0x38, 0xA9, 0, 0xED, *_word(lo), 0x8D, *_word(lo),
            0xA9, 0, 0xED, *_word(hi), 0x8D, *_word(hi),
            "set:", 0xB9, *_word(_FREQ_LO), 0x18, 0x6D, *_word(lo), 0x99, 0x00, 0xD4,  # shadow
            0xB9, *_word(_FREQ_HI), 0x6D, *_word(hi), 0x99, 0x01, 0xD4,                # + offset
            "next:", 0xE8, 0xE0, 3, 0xD0, "loop",
            "done:", 0x60,
        ])
        self.put(state, bytes(6) + bytes(v & 0xFF for v in _LFO) + code)
        self.put(_PLAY_COPY + 1, bytes(_word(table + len(_LFO))))
        return state

    def stub(self, fx, vibrato, state):
        """6502 effect entry, A = effect index: slot 1 = the effect's tracks, priority 0xc0,
        after the virtual voices of slot 0, then 0x316b(y = 1). fx: [[(track, length)] * 1..2];
        one track plays on voice 0, two on voices 0 and 1 (voice 1 taken from the music).
        vibrato: per effect 0 / 1, written to the hook's on flag (state, phase at state + 1)."""
        assert all(1 <= len(t) <= _FX_TRACKS for t in fx) and len(fx) < 32
        counts, masks, vibs = self.end(), self.end() + len(fx), self.end() + 2 * len(fx)
        self.put(counts, bytes(len(t) for t in fx) + bytes((1 << len(t)) - 1 for t in fx)
                 + bytes(vibrato))
        table = self.end()
        for t in fx:
            t = t + [(0, 0)] * (_FX_TRACKS - len(t))
            self.put(self.end(), b"".join(struct.pack("<H", p) for p, _ in t)
                     + b"".join(struct.pack("<H", n) for _, n in t))
        s = self.end()
        code = bytearray([
            0xAA,                                          # tax                 x = index
            0xBD, vibs & 0xFF, vibs >> 8, 0x8D, state & 0xFF, state >> 8,  # vibrato on / off
            0xA9, 0x00, 0x8D, (state + 1) & 0xFF, (state + 1) >> 8,         # phase 0
            0xBD, counts & 0xFF, counts >> 8, 0x8D, 0x2D, 0x27,  # lda counts,x; sta $272d tracks
            0xBD, masks & 0xFF, masks >> 8, 0x8D, 0x3B, 0x27,    # lda masks,x; sta $273b voices
            0x8A, 0x0A, 0x0A, 0x0A, 0xAA,                  # txa; asl x3; tax    x = 8 * index
            0xAD, 0x34, 0x27,                              # lda $2734       slot 0 first voice
            0x18, 0x6D, 0x2C, 0x27,                        # clc; adc $272c  + slot 0 tracks = v
            0x8D, 0x35, 0x27,                              # sta $2735       slot 1 first voice
            0x18, 0x6D, 0x2D, 0x27, 0x8D, 0x27, 0x27,      # clc; adc $272d; sta $2727  in use
            0xAD, 0x35, 0x27, 0x0A, 0xA8,                  # lda $2735; asl; tay y = 2v
        ])
        # track pointers to 0x2768 + 2v, lengths to 0x2778 + 2v
        dests = [0x2768, 0x2769, 0x276A, 0x276B, 0x2778, 0x2779, 0x277A, 0x277B]
        for k, dest in enumerate(dests):
            src = table + k
            code += bytes([0xBD, src & 0xFF, src >> 8, 0x99, dest & 0xFF, dest >> 8])
        code += bytes([
            0xA9, _TEMPO & 0xFF, 0x8D, 0x32, 0x27,              # slot 1 tempo
            0xA9, _TEMPO >> 8, 0x8D, 0x33, 0x27,
            0xA9, 0x7F, 0x8D, 0x2F, 0x27,                      # volume
            0xA9, _FX_PRIORITY, 0x8D, 0x3D, 0x27,              # priority over the music
            0xA9, 0x02, 0x8D, 0x28, 0x27,                      # two slots
            0xA0, 0x01, 0x4C, _SLOT_START & 0xFF, _SLOT_START >> 8,  # ldy #1; jmp $316b
        ])
        self.put(s, bytes(code))
        return s

    def file(self):
        hdr = bytearray(self.hdr)
        hdr[0x0E:0x10] = struct.pack(">H", self.songs)
        end_page = hdr[0x78] + hdr[0x79]
        hdr[0x78] = (self.end() + 0x400 + 0xFF) >> 8          # behind data + alias page
        hdr[0x79] = end_page - hdr[0x78]
        return bytes(hdr) + struct.pack("<H", _LOAD) + bytes(self.pay)


def build(sid_bytes):
    """-> (file bytes, command table [(kind, arg)] * 128, silent song,
    stub address, alias page, ram size, effect index by command, song by command)"""
    tune = _Tune(sid_bytes)
    for slot, (wave, ad, sr, pw) in sfx.INSTRUMENTS.items():
        tune.instrument(slot, bytes([pw & 0xFF, pw >> 8, sr, ad, 0, 0, 0, wave]))
    table = [(_IGNORE, 0)] * 128
    for c, s in _MUSIC.items():
        table[c] = (_SONG, s)
    silent = tune.song(b"STOP", [[0xFF, 0xFF]], mask=0x06)
    for c in _STOP:
        table[c] = (_SONG, silent)
    songs = {}
    for c, tracks in sfx.JINGLES.items():
        songs[c] = tune.song(b"JI%02X" % c, [_track(ns) for ns in tracks])
        table[c] = (_SONG, songs[c])
    fx, index = [], {}
    for c, tracks in sfx.EFFECTS.items():
        _, ptrs, lens = tune.piece(b"FX%02X" % c, [_track(ns) for ns in tracks],
                                   (1 << len(tracks)) - 1, _FX_PRIORITY)
        index[c] = len(fx)
        table[c] = (_EFFECT, len(fx))
        fx.append(list(zip(ptrs, lens)))
    stub = tune.stub(fx, [int(c in sfx.VIBRATO) for c in sfx.EFFECTS], tune.vibrato())
    alias = (tune.end() + 0xFF) & ~0xFF
    ram = alias + 0x400 - _LOAD
    return tune.file(), table, silent, stub, alias, ram, index, songs


def selfcheck():
    assert _chain(5) == [5, 7] and _chain(128) == [0] and _chain(255) == list(range(8))
    assert _track([(2, 3, 60, 11)]) == [0x81, 11, 0x80, 6, 60, 5 | 0, 0xFF, 0xFF]
    sfx.check()
