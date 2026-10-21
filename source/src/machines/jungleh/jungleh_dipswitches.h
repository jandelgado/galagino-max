#ifndef JUNGLEH_DIPSWITCHES_H
#define JUNGLEH_DIPSWITCHES_H

// MAME INPUT_PORTS_START( junglek ) defaults.
// DSW1 (d40a):
//   b0-1 finish bonus (11 = none, 10 = timer x1, 01 = x2, 00 = x3)
//   b2   unused (1 = off)
//   b3-4 lives (11 = 3, 10 = 4, 01 = 5, 00 = 6)
//   b5   service mode (1 = off)
//   b6   flip screen (0 = off)
//   b7   cabinet (0 = upright)
#define JUNGLEH_DSW1  0x3F

// DSW2 (AY#0 port A): b0-3 coin A, b4-7 coin B (0 = 1 coin 1 credit)
#define JUNGLEH_DSW2  0x00

// DSW3 (AY#0 port B):
//   b0-1 bonus life (11 = none, 10 = 10000, 01 = 20000, 00 = 30000)
//   b2-4 unused (1 = off)
//   b5   year display (1 = yes)
//   b6   infinite lives (1 = no)
//   b7   coin slots (1 = A and B)
#define JUNGLEH_DSW3  0xFF

#endif
