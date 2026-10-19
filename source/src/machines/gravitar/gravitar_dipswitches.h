#ifndef GRAVITAR_DIPSWITCHES_H
#define GRAVITAR_DIPSWITCHES_H

// DSW0 (D4, POKEY 1 ALLPOT), MAME gravitar defaults
// bits 0-1 unused
// bits 2-3 lives: 0 = 3, 1 = 4, 2 = 5, 3 = 6
// bit  4   difficulty: 0 = easy, 1 = hard
// bit  5   unused
// bits 6-7 bonus ship: 0 = 10000, 1 = 20000, 2 = 30000, 3 = none
#define GRAVITAR_DSW0 0x04

// DSW1 (B4, POKEY 2 ALLPOT)
// bits 0-1 coinage: 0 = 1 coin 1 credit, 1 = 2 coins 1 credit, 2 = free play, 3 = 1 coin 2 credits
// bits 2-3 right coin mech: 0 = x1, 1 = x4, 2 = x5, 3 = x6
// bit  4   left coin mech: 0 = x1, 1 = x2
// bits 5-7 bonus coins: 0 = none
#define GRAVITAR_DSW1 0x00

#endif
