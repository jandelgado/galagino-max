#ifndef ASTEROIDS_DIPSWITCHES_H
#define ASTEROIDS_DIPSWITCHES_H

// DSW (MAME "asteroid" ports, read 2 bits at a time through a LS153):
//   bits 0-1 language: 0 English, 1 German, 2 French, 3 Spanish
//   bit 2 lives: 1 = 3, 0 = 4
//   bit 3 center coin mech: 0 x1, 1 x2
//   bits 4-5 right coin mech: 0 x1, 1 x4, 2 x5, 3 x6
//   bits 6-7 coinage: 3 2C/1C, 2 1C/1C, 1 1C/2C, 0 free play
#define ASTEROIDS_DSW 0x84 // English, 3 lives, 1 coin 1 credit

#endif
