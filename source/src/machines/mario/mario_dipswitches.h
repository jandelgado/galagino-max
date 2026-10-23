#ifndef _mario_dipswitches_h_
#define _mario_dipswitches_h_

// DSW at 0x7f80, active high (MAME mario.cpp, US rev G)
#define MARIO_DIP_LIVES3      0b00000000  // bits 0-1: 3, 4, 5, 6 lives
#define MARIO_DIP_LIVES4      0b00000001
#define MARIO_DIP_LIVES5      0b00000010
#define MARIO_DIP_LIVES6      0b00000011

#define MARIO_DIP_1C1P        0b00000000  // bits 2-3: coinage
#define MARIO_DIP_2C1P        0b00000100
#define MARIO_DIP_1C2P        0b00001000
#define MARIO_DIP_1C3P        0b00001100

#define MARIO_DIP_BONUS20K    0b00000000  // bits 4-5: one bonus life
#define MARIO_DIP_BONUS30K    0b00010000
#define MARIO_DIP_BONUS40K    0b00100000
#define MARIO_DIP_BONUS_NONE  0b00110000

#define MARIO_DIP_EASY        0b00000000  // bits 6-7: difficulty
#define MARIO_DIP_MEDIUM      0b10000000
#define MARIO_DIP_HARD        0b01000000
#define MARIO_DIP_HARDEST     0b11000000

#define MARIO_DIP  (MARIO_DIP_LIVES3 | MARIO_DIP_1C1P | MARIO_DIP_BONUS20K | MARIO_DIP_EASY)

#endif
