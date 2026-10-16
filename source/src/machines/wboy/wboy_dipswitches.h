#ifndef WBOY_DIPSWITCHES_H
#define WBOY_DIPSWITCHES_H

// SWA (port 0x0c): b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define WBOY_SWA 0xff

// SWB (port 0x0d / 0x10), MAME INPUT_PORTS_START( wboy ):
// b0    cabinet: 0 = upright
// b1    demo sounds: 0 = on
// b2-3  lives: 11 = 3, 10 = 4, 01 = 5, 00 = free play
// b4    bonus life: 1 = 30k 100k 170k 240k, 0 = 30k 120k 210k 300k
// b5    allow continue: 1 = yes
// b6    difficulty: 1 = easy, 0 = hard
// b7    unused
#define WBOY_SWB 0xec

#endif
