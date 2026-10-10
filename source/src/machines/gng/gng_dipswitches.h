#ifndef GNG_DIPSWITCHES_H
#define GNG_DIPSWITCHES_H

// Raw port values as read by the CPU (MAME gng.cpp INPUT_PORTS gng)

// DSW1 ($3003)
#define GNG_DIP_COINAGE_1C1C   0b00001111
#define GNG_DIP_COIN_A         0b00010000 // coinage affects coin A
#define GNG_DIP_DEMO_SOUNDS    0b00000000 // bit 5 low: on
#define GNG_DIP_NO_SERVICE     0b01000000
#define GNG_DIP_NO_FLIP        0b10000000

#define GNG_DIP_A (GNG_DIP_COINAGE_1C1C | GNG_DIP_COIN_A | GNG_DIP_DEMO_SOUNDS | \
                   GNG_DIP_NO_SERVICE | GNG_DIP_NO_FLIP)

// DSW2 ($3004)
#define GNG_DIP_LIVES_3        0b00000011
#define GNG_DIP_UPRIGHT        0b00000000
#define GNG_DIP_BONUS_20K_70K  0b00011000 // 20K 70K every 70K
#define GNG_DIP_NORMAL         0b01100000 // difficulty
#define GNG_DIP_UNUSED         0b10000000

#define GNG_DIP_B (GNG_DIP_LIVES_3 | GNG_DIP_UPRIGHT | GNG_DIP_BONUS_20K_70K | \
                   GNG_DIP_NORMAL | GNG_DIP_UNUSED)

#endif
