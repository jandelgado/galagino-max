#ifndef REGULUS_DIPSWITCHES_H
#define REGULUS_DIPSWITCHES_H

// SWA (port 0x0c): b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define REGULUS_SWA 0xff

// SWB (port 0x0d / 0x10), MAME INPUT_PORTS_START( regulus ):
// b0    cabinet: 0 = upright
// b1    unused
// b2-3  lives: 11 = 3, 10 = 4, 01 = 5, 00 = infinite
// b4-5  unused
// b6    difficulty: 1 = easy, 0 = hard
// b7    allow continue: 0 = yes, 1 = no
#define REGULUS_SWB 0x7e

#endif
