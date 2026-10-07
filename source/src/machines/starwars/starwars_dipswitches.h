#ifndef STARWARS_DIPSWITCHES_H
#define STARWARS_DIPSWITCHES_H

// DSW0 (0x4340), MAME starwars defaults
// bits 0-1 starting shields: 0 = 6, 1 = 7, 2 = 8, 3 = 9
// bits 2-3 difficulty: 0 = easy, 1 = moderate, 2 = hard, 3 = hardest
// bits 4-5 bonus shields: 0 = 0, 1 = 1, 2 = 2, 3 = 3
// bit  6   demo sounds: 0 = on, 1 = off
// bit  7   freeze: 0 = on, 1 = off
#define STARWARS_DSW0 0x96

// DSW1 (0x4360)
// bits 0-1 coinage: 0 = free play, 1 = 1 coin 2 credits, 2 = 1 coin 1 credit, 3 = 2 coins 1 credit
// bits 2-3 right coin mech: 0 = x1, 1 = x4, 2 = x5, 3 = x6
// bit  4   left coin mech: 0 = x1, 1 = x2
// bits 5-7 bonus coin adder: 0 = none
#define STARWARS_DSW1 0x02

#endif
