#ifndef QBERT_DIPSWITCHES_H
#define QBERT_DIPSWITCHES_H

// MAME INPUT_PORTS_START( qbert ) DSW (0x5800) defaults:
//   b0 demo sounds (0 = on)        b1 kicker (1 = on)
//   b2 cabinet (0 = upright)       b3 demo mode, unlimited lives (0 = off)
//   b4 free play (0 = off)         b5-7 unused
#define QBERT_DSW  0x02
#define QBERT_DSW_DEMO_SOUND_OFF  0x01

#endif
