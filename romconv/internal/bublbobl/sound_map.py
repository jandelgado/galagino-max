# Bubble Bobble sound latch ($FA00) -> galagino sound. Commands not listed
# are ignored. Source: capture_sound.lua (MODE=log: commands the game
# sends, MODE=sweep: what each command sounds like), split_sound.py.
#
# Sound driver commands seen in a 12000 frame log (first frame, count):
#   ef 00 2d  boot (driver init after sound reset; 00 also every ~600
#             frames in attract: silent, stops music)
#   34  302   1   coin          07  311  1   after start (music)
#   31  902  85                 2c  917 79
#   20 1230  22                 25 1475 18
#   0d 1599   4                 02 3685  1   silent
#   08 3695   1   (music)       0b 4007  1
#   11 5326  11                 26 5495  3
#   1e 10749  1
# Audible in the sweep, never seen in the log (later rounds?): 09 0a 0c
# 0e 0f 10 12-1d 21-24 27-2b 2f 30 32 33 f2 f4 f6.

# command: SID subtune 1..12 (Bubble_Bobble.sid), arcade cue. Subtunes
# chosen by ear (user); 30 = 07 without the intro (spectrogram match).
MUSIC = {
    0x07: 1,   # game start: intro, then main theme ("tune 1")
    0x08: 9,   # game over, after 02 ("tune 2")
    0x30: 6,   # main theme ("main tune")
}

# commands that silence the music
STOP = {
    0x00,
    0x02,
}

# command: name; sample = cmd_xx.wav in samples/bublbobl_samples.zip
EFFECTS = {
    0x0b: "fx0b",
    0x0d: "fx0d",
    0x11: "fx11",
    0x1e: "fx1e",
    0x20: "fx20",
    0x25: "fx25",
    0x26: "fx26",
    0x2c: "fx2c",
    0x2d: "fx2d",
    0x31: "fx31",
    0x34: "coin",
}
