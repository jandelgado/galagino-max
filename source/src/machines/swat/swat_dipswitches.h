#ifndef SWAT_DIPSWITCHES_H
#define SWAT_DIPSWITCHES_H

// SWA (port 0x0c): b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define SWAT_SWA 0xff

// SWB (port 0x0d / 0x10), MAME INPUT_PORTS_START( swat ):
// b0    cabinet: 0 = upright
// b1-2  lives: 11 = 3, 10 = 4, 01 = 5, 00 = infinite
// b3-5  bonus life: 111 = 30000 .. 001 = 90000, 000 = none
// b6    unused
// b7    SW 0 read from (system1_generic default)
#define SWAT_SWB 0xfe

#endif
