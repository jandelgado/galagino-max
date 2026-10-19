#ifndef GARDIA_DIPSWITCHES_H
#define GARDIA_DIPSWITCHES_H

// SWA (port 0x0c): b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define GARDIA_SWA 0xff

// SWB (port 0x0d / 0x10), MAME INPUT_PORTS_START( gardia ):
// b0    cabinet: 0 = upright
// b1    demo sounds: 0 = on
// b2-3  lives: 11 = 3, 10 = 4, 01 = 5, 00 = infinite
// b4-5  bonus life: 11 = 5k 20k 30k, 10 = 10k 25k 50k, 01 = 15k 30k 60k,
//       00 = none
// b6    difficulty: 1 = easy, 0 = hard
// b7    unknown: 0 = on (MAME default)
#define GARDIA_SWB 0x7c

#endif
