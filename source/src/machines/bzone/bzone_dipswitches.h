#ifndef BZONE_DIPSWITCHES_H
#define BZONE_DIPSWITCHES_H

// DSW0 (0x0a00), MAME bzone defaults
// bits 0-1 lives: 0 = 2, 1 = 3, 2 = 4, 3 = 5
// bits 2-3 missile appears at: 0 = 5000, 1 = 10000, 2 = 20000, 3 = 30000
// bits 4-5 bonus tank: 0 = none, 1 = 15k and 100k, 2 = 25k and 100k, 3 = 50k and 100k
// bits 6-7 language: 0 = English, 1 = German, 2 = French, 3 = Spanish
#define BZONE_DSW0 0x15

// DSW1 (0x0c00)
// bits 0-1 coinage: 0 = free play, 1 = 1 coin 2 credits, 2 = 1 coin 1 credit, 3 = 2 coins 1 credit
// bits 2-3 right coin mech: 0 = x1, 1 = x4, 2 = x5, 3 = x6
// bit  4   left coin mech: 0 = x1, 1 = x2
// bits 5-7 bonus coins: 0 = none
// 1 coin 1 credit (MAME default 0x03 is 2 coins 1 credit): START's
// virtual coin gives one credit
#define BZONE_DSW1 0x02

#endif
