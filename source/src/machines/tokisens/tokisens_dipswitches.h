#ifndef TOKISENS_DIPSWITCHES_H
#define TOKISENS_DIPSWITCHES_H

// MAME INPUT_PORTS_START( tokisens ) includes choplift: port 0x0c (MAME
// "SWA", engine dswA) holds the game options, ports 0x0d / 0x10 ("SWB",
// dswB) the coinage.

// port 0x0c:
// b0    cabinet: 0 = upright
// b1    demo sounds: 0 = on
// b2-3  lives: 00 = 1, 10 = 2, 11 = 3, 01 = 4
// b4-7  unknown, high (MAME default)
#define TOKISENS_OPTIONS 0xfc

// ports 0x0d / 0x10: b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define TOKISENS_COINAGE 0xff

#endif
