#ifndef PITFALL2_DIPSWITCHES_H
#define PITFALL2_DIPSWITCHES_H

// SWA (port 0x0c): b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define PITFALL2_SWA 0xff

// SWB (port 0x0d / 0x10), MAME INPUT_PORTS_START( pitfall2u ), default 0xde:
// b0    cabinet: 0 = upright
// b1-2  lives: 11 = 3, 10 = 4, 01 = 5, 00 = infinite
// b3-4  starting stage: 11 = 1, 10 = 2, 01 = 3, 00 = 4
// b5    allow continue: 0 = yes
// b6    time: 1 = 3 minutes, 0 = 2 minutes
// b7    unused
#define PITFALL2_SWB 0xde

#endif
