#ifndef IMSORRY_DIPSWITCHES_H
#define IMSORRY_DIPSWITCHES_H

// SWA (port 0x0c): b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define IMSORRY_SWA 0xff

// SWB (port 0x0d / 0x10), MAME INPUT_PORTS_START( imsorry ):
// b0    cabinet: 0 = upright
// b1    demo sounds: 0 = on
// b2-3  lives: 11 = 3, 10 = 4, 01 = 5, 00 = infinite
// b4-5  bonus life: 11 = 30000, 10 = 40000, 01 = 50000, 00 = none
// b6    unused
// b7    SW 0 read from: 1 = port 0x0d
#define IMSORRY_SWB 0xfc

#endif
