#ifndef GBERET_DIPSWITCHES_H
#define GBERET_DIPSWITCHES_H

// DSW1 (read f600): b0-3 coin A, b4-7 coin B; 0xf = 1 coin 1 credit
#define GBERET_DSW1 0xff

// DSW2 (read f200), MAME INPUT_PORTS_START( gberet ):
// b0-1  lives: 11 = 2, 10 = 3, 01 = 5, 00 = 7
// b2    cabinet: 0 = upright
// b3-4  bonus: 11 = 30K/70K/every 70K, 10 = 40K/80K/every 80K,
//       01 = 50K/100K/every 100K, 00 = 50K/200K/every 200K
// b5-6  difficulty: 11 = easy, 10 = normal, 01 = difficult, 00 = very difficult
// b7    demo sounds: 0 = on, set at runtime from input->demoSoundsOff()
#define GBERET_DSW2 0x4a
#define GBERET_DSW2_DEMO_SOUND_OFF 0x80

// DSW3 (read f400): b0 flip screen (1 = off), b1 upright controls (1 = single),
// b2-3 unused (1)
#define GBERET_DSW3 0x0f

#endif
