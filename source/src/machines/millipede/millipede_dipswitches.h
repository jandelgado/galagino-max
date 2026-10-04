#ifndef MILLIPEDE_DIPSWITCHES_H
#define MILLIPEDE_DIPSWITCHES_H

// DSW1 (POKEY 1 ALLPOT, 0x0408, MAME "milliped" ports, value read as is):
//   bit 0 millipede head: 0 easy, 1 hard
//   bit 1 beetle: 0 easy, 1 hard
//   bits 2-3 lives: 0 = 2, 1 = 3, 2 = 4, 3 = 5
//   bits 4-5 bonus life: 0 = 12000, 1 = 15000, 2 = 20000, 3 none
//   bit 6 spider: 0 easy, 1 hard
//   bit 7 starting score select: 0 on, 1 off
#define MILLIPEDE_DSW1 0x14 // easy, 3 lives, 15000, select on

// DSW2 (POKEY 2 ALLPOT, 0x0808):
//   bits 0-1 coinage: 3 2C/1C, 2 1C/1C, 1 1C/2C, 0 free play
//   bits 2-3 right coin multiplier: 0 x1, 1 x4, 2 x5, 3 x6
//   bit 4 left coin multiplier: 0 x1, 1 x2
//   bits 5-7 bonus coins: 0 none, 6 demo mode
#define MILLIPEDE_DSW2 0x02 // 1 coin 1 credit

// P8 switches, IN0/IN1 bits 0-3 while TBEN (outlatch 5) is low:
//   IN0 bits 0-1 language: 0 English, 1 German, 2 French, 3 Spanish
//   IN0 bits 2-3 select mode starting scores: 0 = 0, 1 = 0 1x, 2 = 0 1x 2x, 3 = 0 1x 2x 3x
//   IN1 bit 2 credit minimum: 0 = 1, 1 = 2
//   IN1 bit 3 coin counters: 0 = 1, 1 = 2
#define MILLIPEDE_P8_IN0 0x04 // English, 0 1x
#define MILLIPEDE_P8_IN1 0x00

#endif
