#ifndef CHOPLIFT_DIPSWITCHES_H
#define CHOPLIFT_DIPSWITCHES_H

// MAME INPUT_PORTS_START( choplift ) swaps the banks: port 0x0c (MAME
// "SWA", engine dswA) holds the game options, ports 0x0d / 0x10 ("SWB",
// dswB) the coinage.

// port 0x0c:
// b0    cabinet: 0 = upright
// b1    demo sounds: 0 = on
// b2-3  lives: 10 = 2, 11 = 3, 01 = 4, 00 = free play
// b4    bonus life: 1 = 20k 70k 120k 170k, 0 = 50k 100k 150k 200k
// b5    difficulty: 0 = hard (MAME default), 1 = easy
// b6-7  unused, high
#define CHOPLIFT_OPTIONS 0xdc

// ports 0x0d / 0x10: b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define CHOPLIFT_COINAGE 0xff

#endif
