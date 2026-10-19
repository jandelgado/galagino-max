#ifndef MRVIKING_DIPSWITCHES_H
#define MRVIKING_DIPSWITCHES_H

// SWA (port 0x0c): b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define MRVIKING_SWA 0xff

// SWB (port 0x0d / 0x10), MAME INPUT_PORTS_START( mrviking ):
// b0    cabinet: 0 = upright
// b1    maximum credits: 1 = 9, 0 = 99
// b2-3  lives: 11 = 3, 10 = 4, 01 = 5, 00 = infinite
// b4-5  bonus life: 11 = 10k 30k every 30k, 10 = 20k 40k, 01 = 30k, 00 = 40k
// b6    difficulty: 1 = easy, 0 = hard
// b7    SW 0 read from: 1 = port 0x0d
#define MRVIKING_SWB 0xfc

#endif
