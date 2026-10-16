#ifndef FOURDWARRIO_DIPSWITCHES_H
#define FOURDWARRIO_DIPSWITCHES_H

// SWA (port 0x0c): b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define FOURDWARRIO_SWA 0xff

// SWB (port 0x0d / 0x10), MAME INPUT_PORTS_START( 4dwarrio ):
// b0    cabinet: 0 = upright
// b1-2  lives: 11 = 3, 10 = 4, 01 = 5, 00 = infinite
// b3-5  bonus life: 111 = 30000 .. 001 = 90000, 000 = none
// b6    difficulty: 1 = easy, 0 = hard
// b7    SW 0 read from: 1 = port 0x0d
#define FOURDWARRIO_SWB 0xfe

#endif
