#ifndef UPNDOWN_DIPSWITCHES_H
#define UPNDOWN_DIPSWITCHES_H

// SWA (port 0x0c): b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define UPNDOWN_SWA 0xff

// SWB (port 0x0d / 0x10), MAME INPUT_PORTS_START( upndown ):
// b0    cabinet: 0 = upright
// b1-2  lives: 11 = 3, 10 = 4, 01 = 5, 00 = infinite
// b3-5  bonus life: 111 = 10000, 110 = 20000 ... 001 = 70000, 000 = none
// b6-7  difficulty: 11 = easy, 10 = medium, 01 = hard, 00 = hardest
#define UPNDOWN_SWB 0xfe

#endif
