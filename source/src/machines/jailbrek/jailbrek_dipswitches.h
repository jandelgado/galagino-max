#ifndef JAILBREK_DIPSWITCHES_H
#define JAILBREK_DIPSWITCHES_H

// MAME konami/jailbrek.cpp INPUT_PORTS_START( jailbrek ), active low

// DSW1 (0x3303): coinage, 0xff = 1 coin 1 credit on both slots
#define JAILBREK_DSW1 0xff

// DSW2 (0x3100)
#define JAILBREK_DSW2_LIVES_3        0x01  // b0-1: 03 = 1, 02 = 2, 01 = 3, 00 = 5
#define JAILBREK_DSW2_UPRIGHT        0x00  // b2: 0 upright, 1 cocktail
#define JAILBREK_DSW2_BONUS_30K_70K  0x08  // b3: 1 = 30K 70K+, 0 = 40K 80K+
#define JAILBREK_DSW2_DIFFICULT      0x10  // b4-5: 30 easy, 20 normal, 10 difficult, 00 very
#define JAILBREK_DSW2_UNUSED         0x40  // b6
#define JAILBREK_DSW2_DEMO_SOUND_OFF 0x80  // b7: 0 = demo sounds on
#define JAILBREK_DSW2 (JAILBREK_DSW2_LIVES_3 | JAILBREK_DSW2_UPRIGHT | \
                       JAILBREK_DSW2_BONUS_30K_70K | JAILBREK_DSW2_DIFFICULT | \
                       JAILBREK_DSW2_UNUSED)

// DSW3 (0x3200): b0 flip off, b1 single upright controls, b2-3 unused
#define JAILBREK_DSW3 0x0f

#endif
