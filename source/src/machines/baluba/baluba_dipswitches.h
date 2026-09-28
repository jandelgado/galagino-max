#ifndef _baluba_dipswitches_h_
#define _baluba_dipswitches_h_

// DSW1 (0xD004), MAME defaults
//   b0-1 coin A 1C1C, b2-3 coin B 1C1C, b4-5 lives 3,
//   b6 cabinet (1 = upright), b7 unknown (default 1)
#define BALUBA_DSW1  0xC0

// DSW2 (0xD005)
//   b0-2 bonus life 30k/100k/200k, b3-5 difficulty 0, b6-7 unknown
#define BALUBA_DSW2  0x00

#endif
