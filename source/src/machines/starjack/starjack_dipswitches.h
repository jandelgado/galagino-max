#ifndef STARJACK_DIPSWITCHES_H
#define STARJACK_DIPSWITCHES_H

// SWA (port 0x0c): b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define STARJACK_SWA 0xff

// SWB (port 0x0d / 0x10), MAME INPUT_PORTS_START( starjack ):
// b0    cabinet: 0 = upright
// b1-2  lives: 11 = 3, 10 = 4, 01 = 5, 00 = infinite
// b3-5  bonus: 110 = 20k then every 30k (111 every 20k, 101 every 30k, ...)
// b6-7  difficulty: 11 = easy, 10 = medium, 01 = hard, 00 = hardest
#define STARJACK_SWB 0xf6

#endif
