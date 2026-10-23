#ifndef TYLZ_DIPSWITCHES_H
#define TYLZ_DIPSWITCHES_H

// MAME INPUT_PORTS_START( tylz ) DSW (0x5800) defaults:
//   b0+b4 coinage (0 = 1 coin 1 credit)   b1+b5 bonus life (0 = 15k 35k, every 20k)
//   b2 lives (0 = 3)                      b3 demo sounds (1 = on)
//   b6-7 difficulty (0 = easy)
#define TYLZ_DSW  0x08
#define TYLZ_DSW_DEMO_SOUND_ON  0x08

#endif
