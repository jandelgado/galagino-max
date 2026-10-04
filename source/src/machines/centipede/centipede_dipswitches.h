#ifndef CENTIPEDE_DIPSWITCHES_H
#define CENTIPEDE_DIPSWITCHES_H

// DSW1 (0x0800, MAME "centiped" ports, value read as is):
//   bits 0-1 language: 0 English, 1 German, 2 French, 3 Spanish
//   bits 2-3 lives: 0 = 2, 1 = 3, 2 = 4, 3 = 5
//   bits 4-5 bonus life: 0 = 10000, 1 = 12000, 2 = 15000, 3 = 20000
//   bit 6 difficulty: 1 easy, 0 hard
//   bit 7 credit minimum: 0 = 1, 1 = 2
#define CENTIPEDE_DSW1 0x54 // English, 3 lives, 12000, easy, minimum 1

// DSW2 (0x0801):
//   bits 0-1 coinage: 3 2C/1C, 2 1C/1C, 1 1C/2C, 0 free play
//   bits 2-3 right coin multiplier: 0 x1, 1 x4, 2 x5, 3 x6
//   bit 4 left coin multiplier: 0 x1, 1 x2
//   bits 5-7 bonus coins: 0 none
#define CENTIPEDE_DSW2 0x02 // 1 coin 1 credit

#endif
