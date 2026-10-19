#ifndef RAFLESIA_DIPSWITCHES_H
#define RAFLESIA_DIPSWITCHES_H

// SWA (port 0x0c): b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define RAFLESIA_SWA 0xff

// SWB (port 0x0d / 0x10), MAME INPUT_PORTS_START( raflesia ):
// b0    cabinet: 0 = upright
// b1    demo sounds: 0 = on
// b2-3  lives: 11 = 3, 10 = 4, 01 = 5, 00 = infinite
// b4-5  bonus: 11 = 20k/70k/120k, 10 = 30k/80k/150k, 01 = 50k/100k/200k, 00 = none
// b6    unused
// b7    SW 0 read from: 1 = port 0x0d
#define RAFLESIA_SWB 0xfc

#endif
