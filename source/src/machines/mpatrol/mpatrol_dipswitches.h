#ifndef MPATROL_DIPSWITCHES_H
#define MPATROL_DIPSWITCHES_H

// DSW1 (active low)
//   b0-1 lives: 00 = 1, 01 = 2, 10 = 3, 11 = 5
//   b2-3 bonus: 11 = 10k/30k/50k, 10 = 20k/40k/60k, 01 = 10k, 00 = none
//   b4-7 coinage (coin mode 1): 1111 = 1 coin 1 credit
#define MPATROL_DSW1 0xfe
// DSW2 (active low)
//   b0 flip screen (1 = off), b1 cabinet (0 = upright), b2 coin mode (1 = mode 1),
//   b3 unused, b4 stop mode (1 = off), b5 sector selection (1 = off),
//   b6 invulnerability (1 = off), b7 service mode (1 = off)
#define MPATROL_DSW2 0xfd

#endif
