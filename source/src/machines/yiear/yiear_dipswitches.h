#ifndef YIEAR_DIPSWITCHES_H
#define YIEAR_DIPSWITCHES_H

// MAME konami/yiear.cpp INPUT_PORTS_START( yiear ), active low

// DSW1 (0x4e03): coinage, 0xff = 1 coin 1 credit on both slots
#define YIEAR_DSW1 0xff

// DSW2 (0x4c00)
#define YIEAR_DSW2_LIVES_3        0x01  // b0-1: 03 = 1, 02 = 2, 01 = 3, 00 = 5
#define YIEAR_DSW2_UPRIGHT        0x00  // b2: 0 upright, 1 cocktail
#define YIEAR_DSW2_BONUS_30K_80K  0x08  // b3: 1 = 30000 80000, 0 = 40000 90000
#define YIEAR_DSW2_NORMAL         0x10  // b4-5: 30 easy, 10 normal, 20 difficult, 00 very
#define YIEAR_DSW2_UNUSED         0x40  // b6
#define YIEAR_DSW2_DEMO_SOUND_OFF 0x80  // b7: 0 = demo sounds on
#define YIEAR_DSW2 (YIEAR_DSW2_LIVES_3 | YIEAR_DSW2_UPRIGHT | YIEAR_DSW2_BONUS_30K_80K | \
                    YIEAR_DSW2_NORMAL | YIEAR_DSW2_UNUSED)

// DSW3 (0x4d00): b0 flip off, b1 single upright controls, b2 service off,
// b3-7 unused
#define YIEAR_DSW3 0xff

#endif
