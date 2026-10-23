#ifndef JUNOFRST_DIPSWITCHES_H
#define JUNOFRST_DIPSWITCHES_H

// MAME INPUT_PORTS_START( junofrst ) defaults.
// DSW1 (0x802c): b0-3 coin A, b4-7 coin B (0xf = 1 coin 1 credit)
#define JUNOFRST_DSW1  0xFF

// DSW2 (0x8010):
//   b0-1 lives (11 = 3, 10 = 4, 01 = 5, 00 = 256)
//   b2   cabinet (0 = upright)
//   b3   unused (1)
//   b4-6 difficulty (111 = 1 easiest .. 000 = 8 hardest)
//   b7   demo sounds (0 = on)
#define JUNOFRST_DSW2  0x7B
#define JUNOFRST_DSW2_DEMO_SOUND_OFF  0x80

#endif
