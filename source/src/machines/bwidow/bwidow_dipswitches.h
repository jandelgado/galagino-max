#ifndef BWIDOW_DIPSWITCHES_H
#define BWIDOW_DIPSWITCHES_H

// DSW0 (D4, POKEY 1 ALLPOT), MAME bwidow defaults
// bits 0-1 coinage: 0 = 1 coin 1 credit, 1 = 2 coins 1 credit, 2 = free play, 3 = 1 coin 2 credits
// bits 2-3 right coin mech: 0 = x1, 1 = x4, 2 = x5, 3 = x6
// bit  4   left coin mech: 0 = x1, 1 = x2
// bits 5-7 bonus coins: 0 = none
#define BWIDOW_DSW0 0x00

// DSW1 (B4, POKEY 2 ALLPOT)
// bits 0-1 max start: 0 = level 13, 1 = 21, 2 = 37, 3 = 53
// bits 2-3 lives: 0 = 3, 1 = 4, 2 = 5, 3 = 6
// bits 4-5 difficulty: 0 = easy, 1 = medium, 2 = hard, 3 = demo
// bits 6-7 bonus spider: 0 = 20000, 1 = 30000, 2 = 40000, 3 = none
#define BWIDOW_DSW1 0x11

#endif
