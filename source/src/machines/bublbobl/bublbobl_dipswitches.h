#ifndef BUBLBOBL_DIPSWITCHES_H
#define BUBLBOBL_DIPSWITCHES_H

// MAME INPUT_PORTS_START( boblbobl ), defaults
// DSW0 ($FF00)
//   b0+b2  mode: 0x04 game English, 0x05 game Japanese, 0x01/0x00 test
//   b1     flip screen: 1 off
//   b3     demo sounds: 1 on
//   b4-5   coin A: 11 1C1C, 01 2C1C, 10 1C2C, 00 2C3C
//   b6-7   coin B: same encoding
#define BUBLBOBL_DSW0 0xfe

// DSW1 ($FF01)
//   b0-1   difficulty: 11 normal, 10 easy, 01 hard, 00 very hard
//   b2-3   bonus life: 11 30K 100K 400K, 10 20K 80K 300K,
//          01 40K 200K 500K, 00 50K 250K 500K
//   b4-5   lives: 11 3, 10 5, 01 1, 00 2
//   b6-7   monster speed: 00 normal, 01 medium, 10 high, 11 very high
#define BUBLBOBL_DSW1 0x3f

#endif
