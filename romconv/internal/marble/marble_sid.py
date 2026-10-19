"""Marble Madness sound (MARBLE.md "Sound"): MAME captures of the arcade's YM2151 / POKEY
writes per command (tools/mame_sound.lua) become songs of the C64
tune's own player (Marble_Madness.sid, format in MARBLE.md): jingles as songs that replace
the music, effects as one track songs on slot 1 / SID voice 0 started by a 6502 stub over the
music, a silent song for STOP. Timing: tempo 0x3840, duration index k = 2^(7-k) frames,
every length a chain of events (frame exact).

The capture is not distributed: extract() reduces it to the note lists in
marble_sound_notes.json (no ROM or register data), which build() reads.

usage: python3 marble_sid.py --extract CAPTURE.zip   (rewrites marble_sound_notes.json)"""
import json
import math
import os
import struct
import sys
import zipfile

NOTES_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "marble_sound_notes.json")

LOAD = 0x2700
SONG_TABLE, SONGS_MAX = 0x3dc0, 32
TEMPO = 0x3840
INSTR = 0x397a
SLOT_START = 0x316b
NOTE = {0: 0, 1: 1, 2: 2, 4: 3, 5: 4, 6: 5, 8: 6, 9: 7, 10: 8, 12: 9, 13: 10, 14: 11}
KEY_SLOT_BITS = 0x78            # reg 0x08 bits 3-6: operators keyed
TOP_NOTE = 95                   # player note table ends here: higher notes go octaves down

MUSIC = {0x08: 1, 0x0A: 2, 0x0C: 3, 0x0E: 4, 0x14: 5, 0x18: 6}
STOP = (0x00, 0x61)
# jingles: command -> (melody YM channels, sustain YM channels, melody / sustain instrument)
JINGLES = {0x3F: ((0, 1), (6, 5), 11, 6)}
# effects: command -> instrument; noise (YM channel 7 noise or POKEY) uses 12
EFFECTS = {0x2F: 11, 0x30: 11, 0x31: 11, 0x32: 11, 0x33: 11, 0x37: 11, 0x38: 12, 0x39: 11,
           0x3C: 11, 0x3D: 11, 0x3E: 11, 0x42: 0, 0x45: 12, 0x53: 11, 0x5E: 12, 0x5F: 12}
IGNORE, SONG, EFFECT = 0, 1, 2


def midi(kc, kf):
    """YM2151 KC/KF to MIDI (KC 0x4a = A4 = 69)."""
    n = NOTE.get(kc & 15, NOTE.get((kc & 15) - 1, 0))
    return ((kc >> 4) & 7) * 12 + n + 13 + kf / 64


def parse(text):
    regs, ev = {}, []
    for line in text.splitlines():
        p = line.split()
        if not p:
            continue
        if p[0] == "init" and p[1] == "ym":
            regs[int(p[2])] = int(p[3])
        elif p[0] == "game":
            break                                    # the game's own sounds start here
        elif p[0] in ("ym", "pokey"):
            ev.append((float(p[1]), p[0], int(p[2]), int(p[3])))
    ev.sort(key=lambda e: e[0])
    return regs, ev


def ym_notes(text):
    """Key-on notes: [(start frame, length frames or -1, channel, midi)]."""
    regs, ev = parse(text)
    on, out = {}, []
    for t, chip, r, v in ev:
        if chip != "ym":
            continue
        regs[r] = v
        if r != 0x08:
            continue
        c = v & 7
        if c in on:
            t0, m = on.pop(c)
            out.append((t0, t - t0, c, m))
        if v & KEY_SLOT_BITS:
            on[c] = (t, midi(regs.get(0x28 + c, 0), regs.get(0x30 + c, 0) >> 2))
    out += [(t0, -1, c, m) for c, (t0, m) in on.items()]
    return sorted(out)


def ym_pitch_frames(text, frames=240):
    """Per frame: midi of the loudest keyed effect channel (4-7), None if silent; pitch
    changes without key-on (sweeps) included."""
    regs, ev = parse(text)
    keyed, out, i = set(), [], 0
    for k in range(frames):
        while i < len(ev) and ev[i][0] < k + 1:
            _, chip, r, v = ev[i]
            i += 1
            if chip != "ym":
                continue
            regs[r] = v
            if r == 0x08:
                (keyed.add if v & KEY_SLOT_BITS else keyed.discard)(v & 7)
        ch = max((c for c in keyed), default=None, key=lambda c: (c >= 4, -(regs.get(0x78 + c, 127) & 127)))
        out.append(None if ch is None else midi(regs.get(0x28 + ch, 0), regs.get(0x30 + ch, 0) >> 2))
    while out and out[-1] is None:
        out.pop()
    return out


def pokey_pitch_frames(text, frames=240):
    """POKEY effects: per frame the AUDF of the loudest channel as midi (64 kHz clock)."""
    _, ev = parse(text)
    regs, out, i = [0] * 9, [], 0
    for k in range(frames):
        while i < len(ev) and ev[i][0] < k + 1:
            _, chip, r, v = ev[i]
            i += 1
            if chip == "pokey" and r <= 8:
                regs[r] = v
        vols = [regs[2 * c + 1] & 15 for c in range(4)]
        c = max(range(4), key=lambda c: vols[c])
        if not vols[c]:
            out.append(None)
            continue
        hz = 1789773 / 28 / (2 * (regs[2 * c] + 1))
        out.append(69 + 12 * math.log2(hz / 440))
    while out and out[-1] is None:
        out.pop()
    return out


def pokey_sound(text):
    """True when the capture sets a POKEY channel volume (the driver writes silent AUDC
    values all the time)"""
    _, ev = parse(text)
    return any(chip == "pokey" and r <= 7 and r & 1 and v & 15 for _, chip, r, v in ev)


def chain(n):
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


def track(notes, inst):
    """notes [(start, length, midi)], monophonic: -> event bytes."""
    ev, t = [0x81, inst], 0
    for s, l, m in notes:
        if s > t:
            for k in chain(s - t):
                ev += [0x80, k]
            t = s
        ks = chain(l + 1)                       # gate = total - 1 frame
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


def jingle_notes(text, melody, sustain):
    """-> note lists [(start, length, midi)]: melody, then one per sustain channel"""
    ns = ym_notes(text)
    mel = mono(sorted((round(t), round(d) if d > 0 else 128, m) for t, d, c, m in ns if c in melody))
    top = max((m for _, _, m in mel), default=0)
    shift = 0
    while top + shift > TOP_NOTE:
        shift -= 12
    out = [[(s, l, fit(m + shift, TOP_NOTE)) for s, l, m in mel]]
    for c in sustain:
        out.append(mono(sorted((round(t), round(d) if d > 0 else 128, fit(m, TOP_NOTE)) for t, d, cc, m in ns if cc == c)))
    return out


def effect_notes(text):
    """One note list: frames of equal (rounded) pitch become one note."""
    pitches = pokey_pitch_frames(text) if pokey_sound(text) else ym_pitch_frames(text)
    notes, k = [], 0
    while k < len(pitches):
        if pitches[k] is None:
            k += 1
            continue
        n, s = fit(pitches[k], TOP_NOTE), k
        while k < len(pitches) and pitches[k] is not None and fit(pitches[k], TOP_NOTE) == n:
            k += 1
        notes.append((s, k - s, n))
    # track() plays a note for gate + 1 frames: gate = length - 1 keeps the frame timing
    return [(s, max(1, l - 1), n) for s, l, n in notes]


def descriptor(ident, tracks, mask, priority):
    assert len(ident) == 4, ident
    return bytearray(ident) + bytes([TEMPO & 0xFF, TEMPO >> 8, 0x7F, len(tracks), mask, priority])


class Tune:
    def __init__(self, data):
        self.hdr = bytearray(data[:0x7C])
        if data[0x7C] | data[0x7D] << 8 != LOAD:
            raise SystemExit("marble: Marble_Madness.sid load address is not 0x2700")
        self.pay = bytearray(data[0x7E:])
        self.songs = struct.unpack(">H", self.hdr[0x0E:0x10])[0]

    def end(self):
        return LOAD + len(self.pay)

    def put(self, addr, data):
        end = addr - LOAD + len(data)
        if end > len(self.pay):
            self.pay.extend(b"\0" * (end - len(self.pay)))
        self.pay[addr - LOAD:end] = data

    def w(self, addr):
        return self.pay[addr - LOAD] | self.pay[addr - LOAD + 1] << 8

    def piece(self, ident, tracks, mask, priority):
        """append descriptor + tracks; -> (descriptor address, track addresses, lengths)"""
        base = self.end()
        desc = descriptor(ident, tracks, mask, priority)
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
        if self.songs == SONGS_MAX:
            raise SystemExit("marble: C64 song table full")
        base, _, _ = self.piece(ident, tracks, mask, priority)
        self.put(SONG_TABLE + 2 * self.songs, struct.pack("<H", base))
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
            0xA9, TEMPO & 0xFF, 0x8D, 0x32, 0x27,              # slot 1 tempo
            0xA9, TEMPO >> 8, 0x8D, 0x33, 0x27,
            0xA9, 0x7F, 0x8D, 0x2F, 0x27,                      # volume
            0xA9, 0x01, 0x8D, 0x2D, 0x27, 0x8D, 0x3B, 0x27,    # 1 track, voice mask voice 0
            0xA9, 0xC0, 0x8D, 0x3D, 0x27,                      # priority over the music
            0xA9, 0x02, 0x8D, 0x28, 0x27,                      # two slots
            0xA0, 0x01, 0x4C, SLOT_START & 0xFF, SLOT_START >> 8,  # ldy #1; jmp $316b
        ])
        self.put(s, code)
        return s

    def file(self):
        hdr = bytearray(self.hdr)
        hdr[0x0E:0x10] = struct.pack(">H", self.songs)
        end_page = hdr[0x78] + hdr[0x79]
        hdr[0x78] = (self.end() + 0x400 + 0xFF) >> 8          # behind data + alias page
        hdr[0x79] = end_page - hdr[0x78]
        return bytes(hdr) + struct.pack("<H", LOAD) + bytes(self.pay)


def extract(zip_path):
    """MAME capture -> {"jingles": {cmd: [notes per track]}, "effects": {cmd: notes}}"""
    with zipfile.ZipFile(zip_path) as z:
        caps = {int(n[4:6], 16): z.read(n).decode() for n in z.namelist()}
    return {"jingles": {"%02x" % c: jingle_notes(caps[c], mel, sus) for c, (mel, sus, _, _) in JINGLES.items()},
            "effects": {"%02x" % c: effect_notes(caps[c]) for c in EFFECTS}}


def save_notes(notes, path=NOTES_FILE):
    """one note list per line"""
    row = lambda ns: json.dumps([list(n) for n in ns], separators=(",", ":"))
    jingles = [f'    "{c}": [\n' + ",\n".join("      " + row(t) for t in ts) + "\n    ]"
               for c, ts in notes["jingles"].items()]
    effects = [f'    "{c}": {row(ns)}' for c, ns in notes["effects"].items()]
    with open(path, "w", newline="\n") as f:
        f.write('{\n  "jingles": {\n' + ",\n".join(jingles) + '\n  },\n  "effects": {\n'
                + ",\n".join(effects) + "\n  }\n}\n")


def load_notes(path=NOTES_FILE):
    with open(path) as f:
        return json.load(f)


def build(sid_bytes, notes):
    """notes: load_notes() -> (file bytes, command table [(kind, arg)] * 128, silent song,
    stub address, alias page, ram size, effect index by command, song by command)"""
    tune = Tune(sid_bytes)
    table = [(IGNORE, 0)] * 128
    for c, s in MUSIC.items():
        table[c] = (SONG, s)
    silent = tune.song(b"STOP", [[0xFF, 0xFF]], mask=0x06)
    for c in STOP:
        table[c] = (SONG, silent)
    songs = {}
    for c, (mel, sus, im, isus) in JINGLES.items():
        mel_notes, *sus_notes = notes["jingles"]["%02x" % c]
        songs[c] = tune.song(b"JI%02X" % c, [track(mel_notes, im)] + [track(n, isus) for n in sus_notes])
        table[c] = (SONG, songs[c])
    fx, index = [], {}
    for c, inst in EFFECTS.items():
        ns = notes["effects"]["%02x" % c]
        if not ns:
            print(f"marble sound: 0x{c:02x} has no output, ignored")
            continue
        _, ptrs, lens = tune.piece(b"FX%02X" % c, [track(ns, inst)], 0x01, 0xC0)
        index[c] = len(fx)
        table[c] = (EFFECT, len(fx))
        fx.append((ptrs[0], lens[0]))
    stub = tune.stub(fx)
    alias = (tune.end() + 0xFF) & ~0xFF
    ram = alias + 0x400 - LOAD
    return tune.file(), table, silent, stub, alias, ram, index, songs


def selfcheck():
    assert abs(midi(0x4A, 0) - 69) < 1e-9 and abs(midi(0x4E, 0) - 72) < 1e-9
    assert chain(5) == [5, 7] and chain(128) == [0] and chain(255) == list(range(8))
    assert fit(107, 95) == 95 and fit(108, 95) == 84
    assert track([(2, 3, 60)], 11) == [0x81, 11, 0x80, 6, 60, 5 | 0, 0xFF, 0xFF]
    assert mono([(0, 10, 60), (5, 3, 62)]) == [(0, 4, 60), (5, 3, 62)]


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "--extract":
        sys.exit(__doc__)
    save_notes(extract(sys.argv[2]))
    print(f"wrote {NOTES_FILE}")
