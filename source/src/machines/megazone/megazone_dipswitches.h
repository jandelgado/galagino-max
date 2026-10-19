#ifndef MEGAZONE_DIPSWITCHES_H
#define MEGAZONE_DIPSWITCHES_H

// MAME INPUT_PORTS_START( megazone ) defaults.
// DSW1 (sound CPU 0x8001): b0-3 coin A, b4-7 coin B (0xf = 1 coin 1 credit)
#define MEGAZONE_DSW1  0xFF

// DSW2 (sound CPU 0x8000):
//   b0-1 lives (11 = 3, 10 = 4, 01 = 5, 00 = 7)
//   b2   cabinet (0 = upright)
//   b3-4 bonus life (11 = 20k 70k 70k+)
//   b5-6 difficulty (10 = normal)
//   b7   demo sounds (0 = on)
#define MEGAZONE_DSW2  0x5B
#define MEGAZONE_DSW2_DEMO_SOUND_OFF  0x80

#endif
