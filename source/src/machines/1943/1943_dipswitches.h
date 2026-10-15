#ifndef _1943_DIPSWITCHES_H
#define _1943_DIPSWITCHES_H

// Raw port values as read by the CPU (MAME capcom/1943.cpp INPUT_PORTS)

// DSWA
#define _1943_DIP_DIFFICULTY_8     0b00001000 // 1 (easy) .. 16, 8 = normal
#define _1943_DIP_2P_TWO_CREDITS   0b00010000 // 2 players cost 2 credits
#define _1943_DIP_NO_FLIP          0b00100000
#define _1943_DIP_NO_SCREEN_STOP   0b01000000
#define _1943_DIP_NO_SERVICE       0b10000000

#define _1943_DIP_A (_1943_DIP_DIFFICULTY_8 | _1943_DIP_2P_TWO_CREDITS | _1943_DIP_NO_FLIP | \
                     _1943_DIP_NO_SCREEN_STOP | _1943_DIP_NO_SERVICE)

// DSWB
#define _1943_DIP_COIN_A_1C1C      0b00000111
#define _1943_DIP_COIN_B_1C1C      0b00111000
#define _1943_DIP_CONTINUE         0b01000000
#define _1943_DIP_DEMO_SOUNDS      0b10000000

#define _1943_DIP_B (_1943_DIP_COIN_A_1C1C | _1943_DIP_COIN_B_1C1C | _1943_DIP_CONTINUE | \
                     _1943_DIP_DEMO_SOUNDS)

#endif
