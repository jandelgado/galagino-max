#ifndef FLICKY_DIPSWITCHES_H
#define FLICKY_DIPSWITCHES_H

// SWA (port 0x0c): b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define FLICKY_SWA 0xff

// SWB (port 0x0d / 0x10), MAME INPUT_PORTS_START( flickys2 ), default 0xfa:
// b0    cabinet: 0 = upright
// b1    unused
// b2-3  lives: 11 = 2, 10 = 3, 01 = 4, 00 = 5 (infinite)
// b4-5  bonus life: 11 = 80000, 10 = 160000, 01 = 240000, 00 = 320000
// b6    difficulty: 1 = easy, 0 = hard
// b7    SW 0 read from: 1 = port 0x0d
#define FLICKY_SWB 0xfa

#endif
