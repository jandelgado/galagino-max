#ifndef SPACDUEL_DIPSWITCHES_H
#define SPACDUEL_DIPSWITCHES_H

// DSW0 (D4, POKEY 1 ALLPOT), MAME spacduel defaults
// bits 0-1 lives: 0 = 4, 1 = 3, 2 = 6, 3 = 5
// bits 2-3 difficulty: 0 = normal, 1 = easy, 2 = hard, 3 = medium
// bits 4-5 language: 0 = English, 1 = German, 2 = French, 3 = Spanish
// bits 6-7 bonus life: 0 = 10000, 1 = 15000, 2 = none, 3 = 8000
#define SPACDUEL_DSW0 0x01

// DSW1 (B4, POKEY 2 ALLPOT)
// bits 0-1 coinage: 0 = 1 coin 1 credit, 1 = 2 coins 1 credit, 2 = free play, 3 = 1 coin 2 credits
// bits 2-3 right coin mech: 0 = x1, 1 = x4, 2 = x5, 3 = x6
// bit  4   left coin mech: 0 = x1, 1 = x2
// bits 5-7 bonus coins: 0 = none
#define SPACDUEL_DSW1 0x00

// DSW2 (P10/P11, read through 0x0905-0x0907 bit 6), set = off
// bit 0 charge by: 1 = player, 0 = game
// bit 1 2 credit minimum: 1 = off
// bit 2 1 player game only: 1 = off; on here
// bit 3 unused
#define SPACDUEL_DSW2 0x0b

#endif
