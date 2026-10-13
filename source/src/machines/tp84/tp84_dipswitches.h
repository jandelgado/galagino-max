#ifndef TP84_DIPSWITCHES_H
#define TP84_DIPSWITCHES_H

// DSW1 @0x2860: b0-3 coin A, b4-7 coin B; 0xFF = 1C/1C
#define TP84_DSW1  0xFF

// DSW2 @0x3000 (MAME default 0x32):
//   b0-1 lives (0x03=2, 0x02=3, 0x01=5, 0x00=7)
//   b2   cabinet (0=upright)
//   b3-4 bonus (0x18=10k/50k, 0x10=20k/60k, 0x08=30k/70k, 0x00=40k/80k)
//   b5-6 difficulty (0x60 easy, 0x40 normal, 0x20 hard, 0x00 hardest)
//   b7   demo sounds (0=on)
#define TP84_DSW2  0x32
#define TP84_DSW2_DEMO_SOUND_OFF  0x80

#endif
