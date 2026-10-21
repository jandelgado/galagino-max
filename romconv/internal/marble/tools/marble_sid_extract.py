#!/usr/bin/env python3
"""Marble Madness sound notes from a MAME capture (the dev half of romconv.games.marble.sid).

MAME captures of the arcade's YM2151 / POKEY writes per command (tools/mame_sound.lua) are
reduced to the note lists in marble_sound_notes.json (no ROM or register data), which the
converter reads.

usage (romconv/): python3 internal/marble/tools/marble_sid_extract.py --extract CAPTURE.zip
"""
import json
import math
import sys
import zipfile

from devrom import ROMCONV
from romconv.games.marble.sid import EFFECTS, JINGLES, fit, midi, mono

NOTES_FILE = ROMCONV / "romconv" / "games" / "marble" / "marble_sound_notes.json"

INSTR = 0x397a
KEY_SLOT_BITS = 0x78            # reg 0x08 bits 3-6: operators keyed
TOP_NOTE = 95                   # player note table ends here: higher notes go octaves down


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


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "--extract":
        sys.exit(__doc__)
    save_notes(extract(sys.argv[2]))
    print(f"wrote {NOTES_FILE}")
