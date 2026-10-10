#ifndef TEMPEST_DIPSWITCHES_H
#define TEMPEST_DIPSWITCHES_H

// DSW1 (0x0d00, N13), MAME tempest defaults
// bits 0-1 coinage: 0 = 1 coin 1 credit, 1 = 2 coins 1 credit, 2 = free play, 3 = 1 coin 2 credits
// bits 2-3 right coin mech: 0 = x1, 1 = x4, 2 = x5, 3 = x6
// bit  4   left coin mech: 0 = x1, 1 = x2
// bits 5-7 bonus coins: 0 = none
#define TEMPEST_DSW1 0x00

// DSW2 (0x0e00, L12)
// bit  0   minimum: 0 = 1 credit, 1 = 2 credits
// bits 1-2 language: 0 = English, 1 = French, 2 = German, 3 = Spanish
// bits 3-5 bonus life: 0 = 20000, 1 = 10000, 2 = 30000, ..., 6 = 70000, 7 = none
// bits 6-7 lives: 0 = 3, 1 = 4, 2 = 5, 3 = 2
#define TEMPEST_DSW2 0x00

// IN2 bits 0-2 (DE2 on the Mathbox board, read through POKEY 2)
// bits 0-1 difficulty: 0 = Medium2, 1 = Hard, 2 = Easy, 3 = Medium1
// bit  2   rating: 0 = tied to high score, 1 = 1, 3, 5, 7, 9
#define TEMPEST_IN2_DIPS 0x07

#endif
