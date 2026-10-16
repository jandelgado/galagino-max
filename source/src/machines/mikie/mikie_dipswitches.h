#ifndef MIKIE_DIPSWITCHES_H
#define MIKIE_DIPSWITCHES_H

// DSW1 @0x2500: b0-3 coin A, b4-7 coin B; 0xFF = 1C/1C
#define MIKIE_DSW1  0xFF

// DSW2 @0x2501 (MAME default 0x7B):
//   b0-1 lives (0x03=3, 0x02=4, 0x01=5, 0x00=7)
//   b2   cabinet (0=upright)
//   b3-4 bonus (0x18=20k 70k 50k+, 0x10=30k 90k 60k+, 0x08=30k, 0x00=40k)
//   b5-6 difficulty (0x60 easy, 0x40 medium, 0x20 hard, 0x00 hardest)
//   b7   demo sounds (0=on)
#define MIKIE_DSW2  0x7B
#define MIKIE_DSW2_DEMO_SOUND_OFF  0x80

// DSW3 @0x2403 (not mounted): b0 flip off, b1 single upright controls
#define MIKIE_DSW3  0xFE

#endif
