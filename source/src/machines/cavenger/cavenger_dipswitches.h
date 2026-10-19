#ifndef CAVENGER_DIPSWITCHES_H
#define CAVENGER_DIPSWITCHES_H

// DSW0 @0x9002 (MAME default 0xC7):
//   b0-1 difficulty (0x03 easy .. 0x00 hardest)
//   b2   high score names (0x04 = 10 letters)
//   b3   cabinet (0 = upright)
//   b4-5 initial high score (0x00 = 0)
//   b6-7 lives (0xC0 = 3, 0x80 = 4, 0x40 = 5, 0x00 = 2)
#define CAVENGER_DSW0  0xC7

// DSW1 @0x9003: b0-3 coin B, b4-7 coin A; 0xFF = 1C/1C
#define CAVENGER_DSW1  0xFF

#endif
