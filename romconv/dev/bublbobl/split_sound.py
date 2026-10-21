#!/usr/bin/env python3
"""Split the MAME sweep recording into one wav per sound command.

capture_sound.lua MODE=sweep writes sweep.txt (frame stamps) and MAME
-wavwrite writes sweep.wav. Each command window is trimmed to its last
audible sample and stored as cmd_xx.wav (24 kHz mono 16 bit) in the
output zip. Silent commands are skipped. Windows where the game itself
wrote the latch ("game" lines) are flagged: their sound may be cut.

usage: uv run python -m dev.bublbobl.split_sound sweep.wav sweep.txt out.zip
"""
import io
import sys
import wave
import zipfile

import numpy as np

RATE = 24000
FPS = 59.185606  # MAME bublbobl refresh
SILENCE = 300    # int16 peak below this counts as silent
TAIL = RATE // 10


def load_pcm(path):
    with wave.open(path, "rb") as w:
        if w.getframerate() != RATE:
            sys.exit(f"{path}: {w.getframerate()} Hz, expected {RATE}")
        pcm = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
        return pcm.reshape(-1, w.getnchannels()).mean(axis=1)


# (frame, cmd) per command, frames of the following sound resets, and
# frames of latch writes the game made itself
def load_stamps(path):
    cmds, resets, foreign = [], [], []
    for line in open(path):
        f = line.split()
        if f[1] == "cmd":
            cmds.append((int(f[0]), f[2]))
        elif f[1] == "reset":
            resets.append(int(f[0]))
        elif f[1] == "game":
            foreign.append(int(f[0]))
    return cmds, resets, foreign


def main(wav_path, stamps_path, out_zip):
    pcm = load_pcm(wav_path)
    cmds, resets, foreign = load_stamps(stamps_path)

    with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as z:
        for i, (frame, cmd) in enumerate(cmds):
            # window ends at the next sound reset
            end_frame = next((r for r in resets if r > frame), None)
            start = int(frame / FPS * RATE)
            end = int(end_frame / FPS * RATE) if end_frame else len(pcm)
            part = pcm[start:end]
            loud = np.nonzero(np.abs(part) > SILENCE)[0]
            if len(loud) == 0:
                continue

            part = part[: loud[-1] + 1]
            notes = []
            if len(part) >= (end - start) - TAIL:
                notes.append("still playing at window end")
            if any(frame < g < (end_frame or 1 << 30) for g in foreign):
                notes.append("game wrote the latch in this window")
            print(f"cmd {cmd}: {len(part) / RATE:5.2f}s peak {int(np.abs(part).max()):5d}"
                  + (f"  ({', '.join(notes)})" if notes else ""))

            buf = io.BytesIO()
            with wave.open(buf, "wb") as o:
                o.setnchannels(1)
                o.setsampwidth(2)
                o.setframerate(RATE)
                o.writeframes(part.astype(np.int16).tobytes())
            z.writestr(f"cmd_{cmd}.wav", buf.getvalue())


if __name__ == "__main__":
    main(*sys.argv[1:4])
