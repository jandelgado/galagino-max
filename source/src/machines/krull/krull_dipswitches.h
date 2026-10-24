#ifndef KRULL_DIPSWITCHES_H
#define KRULL_DIPSWITCHES_H

// MAME INPUT_PORTS_START( krull ) DSW (0x5800) defaults:
//   b0 demo sounds (0 = on)        b1 difficulty (0 = normal)
//   b3 lives (0 = 3)               b2 + b4 coinage (0 = 1 coin 1 credit)
//   b5 hexagon (0 = roving)        b6-7 bonus (0 = 30k, 80k, every 50k)
#define KRULL_DSW  0x00
#define KRULL_DSW_DEMO_SOUND_OFF  0x01

#endif
