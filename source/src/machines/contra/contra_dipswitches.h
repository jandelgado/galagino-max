#ifndef CONTRA_DIPSWITCHES_H
#define CONTRA_DIPSWITCHES_H

// MAME INPUT_PORTS_START( contra ) defaults (read back from MAME 0.289).
// DSW1 (0x0014): b0-3 coin A, b4-7 coin B (0xf = 1 coin 1 credit)
#define CONTRA_DSW1  0xFF

// DSW2 (0x0015):
//   b0-1 lives (11 = 2, 10 = 3, 01 = 5, 00 = 7)
//   b2   unused
//   b3-4 bonus life (11 = 30000 70000)
//   b5-6 difficulty (10 = normal)
//   b7   demo sounds (0 = on)
#define CONTRA_DSW2  0x5A
#define CONTRA_DSW2_DEMO_SOUND_OFF  0x80

// DSW3 (0x0016): b0 flip (1 = off), b1 unused, b2 service (1 = off),
// b3 sound (1 = stereo), b4-7 not connected (read 0)
#define CONTRA_DSW3  0x0F

#endif
