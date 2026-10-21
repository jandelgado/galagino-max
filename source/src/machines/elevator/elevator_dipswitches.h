#ifndef ELEVATOR_DIPSWITCHES_H
#define ELEVATOR_DIPSWITCHES_H

// MAME elevator defaults.
// DSW1 (d40a):
//   b0-1 bonus life (11 = 10000, 10 = 15000, 01 = 20000, 00 = 25000)
//   b2   free play (1 = off)
//   b3-4 lives (11 = 3, 10 = 4, 01 = 5, 00 = 6)
//   b5   unknown (1 = off)
//   b6   flip screen (1 = off)
//   b7   cabinet (0 = upright)
#define ELEVATOR_DSW1  0x7F

// DSW2 (AY#0 port A): b0-3 coin A, b4-7 coin B (0 = 1 coin 1 credit)
#define ELEVATOR_DSW2  0x00

// DSW3 (AY#0 port B):
//   b0-1 difficulty (11 = easiest, 00 = hard)
//   b2-3 unknown (1 = off)
//   b4   coinage display (1 = coins/credits)
//   b5   year display (1 = yes)
//   b6   hit detection (1 = normal game)
//   b7   coin slots (1 = A and B)
#define ELEVATOR_DSW3  0xFF

#endif
