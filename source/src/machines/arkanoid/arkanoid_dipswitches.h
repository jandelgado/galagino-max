#ifndef ARKANOID_DIPSWITCHES_H
#define ARKANOID_DIPSWITCHES_H

// DSW, AY port B (MAME arkangc default 0x7F):
//   b0 ball speed (1 = normal, 0 = faster, level 2 as level 30)
//   b1 flip screen (1 = off)
//   b2 unused
//   b3 difficulty (1 = easy)
//   b4 bonus life (1 = 20K 60K 60K+, 0 = 20K)
//   b5 lives (1 = 3, 0 = 5)
//   b6 coinage (1 = 1C1C, 0 = 1C2C)
//   b7 cabinet (0 = upright)
#define ARKANOID_DSW  0x7F

#endif
