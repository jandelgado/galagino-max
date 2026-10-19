#ifndef MHAVOC_DIPSWITCHES_H
#define MHAVOC_DIPSWITCHES_H

// DSW1 (13/14S, POKEY 0 ALLPOT), MAME mhavoc defaults
// bit  0   adaptive difficulty: 0 = on, 1 = off
// bit  1   demo sounds: 0 = on, 1 = off (set from Input::demoSoundsOff())
// bits 2-3 bonus life: 0 = 100000, 1 = 200000, 2 = none, 3 = 50000
// bits 4-5 difficulty: 0 = medium, 1 = easy, 2 = demo, 3 = hard
// bits 6-7 lives: 0 = 3, 1 = 6, 2 = 5, 3 = 4
#define MHAVOC_DSW1 0x00
#define MHAVOC_DSW1_DEMO_SOUNDS_OFF 0x02

// DSW2 (8S, gamma 0x4000)
// bits 0-1 coinage: 0 = 1 coin 2 credits, 1 = free play, 2 = 2 coins 1 credit, 3 = 1 coin 1 credit
// bits 2-3 right coin mech: 0 = x6, 1 = x5, 2 = x4, 3 = x1
// bit  4   left coin mech: 0 = x2, 1 = x1
// bits 5-7 bonus credits: 7 = none
#define MHAVOC_DSW2 0xff

// alpha IN0 bits 6-7 while out0 bit 5 (player 1) is set, active low
// bit 0 credit to start: 1 = off
// bit 1 service mode: 1 = off
#define MHAVOC_SERVICE 0x03

#endif
