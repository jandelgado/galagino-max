#ifndef MISSILE_DIPSWITCHES_H
#define MISSILE_DIPSWITCHES_H

// R10 pricing (0x4a00), MAME INPUT_PORTS_START( missile )
// b0-1 coinage: 0 = 1 coin 1 play (MAME default is 2 coins 1 play)
// b2-3 right coin mech x1, b4 center coin mech x1: 0
// b5-6 language: 0 = English
// b7 unused: 1 = off
#define MISSILE_R10 0x80

// R8 game options (POKEY ALLPOT)
// b0-1 cities: 3 = 6
// b2 bonus credit for 4 coins: 0 = yes
// b3 trackball size: 0 = mini (faster cursor)
// b4-6 bonus city: 7 = every 10000
// b7 cabinet: 0 = upright
#define MISSILE_R8 0x73

#endif
