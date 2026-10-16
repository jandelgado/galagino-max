#ifndef HVYMETAL_DIPSWITCHES_H
#define HVYMETAL_DIPSWITCHES_H

// SWA (port 0x0c): b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define HVYMETAL_SWA 0xff

// SWB (port 0x0d / 0x10), MAME INPUT_PORTS_START( hvymetal ):
// b0    cabinet: 0 = upright
// b1    demo sounds: 0 = on
// b2-3  lives: 11 = 3, 10 = 4, 01 = 5, 00 = infinite
// b4-5  bonus life: 11 = 50000 100000, 10 = 60000 120000, 01 = 70000 150000,
//       00 = 100000
// b6    difficulty: 1 = easy, 0 = hard
// b7    allow continue: 0 = yes
#define HVYMETAL_SWB 0x7c

#endif
