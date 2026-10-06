#ifndef THEPIT_DIPSWITCHES_H
#define THEPIT_DIPSWITCHES_H

// DSW at 0xb000 (MAME thepit, default 0x00)
// bit | 0 1     | 2     | 3          | 4    | 5       | 6     | 7          |
//     | coinage | speed | time limit | flip | cabinet | lives | diagnostic |
#define THEPIT_DSW_1C_1C       0x00
#define THEPIT_DSW_3C_2C       0x01
#define THEPIT_DSW_2C_1C       0x02
#define THEPIT_DSW_FREE_PLAY   0x03
#define THEPIT_DSW_SPEED_FAST  0x00
#define THEPIT_DSW_SPEED_SLOW  0x04
#define THEPIT_DSW_TIME_LONG   0x00
#define THEPIT_DSW_TIME_SHORT  0x08
#define THEPIT_DSW_UPRIGHT     0x00
#define THEPIT_DSW_3_LIVES     0x00
#define THEPIT_DSW_4_LIVES     0x40

#define THEPIT_DSW_VALUE (THEPIT_DSW_1C_1C | THEPIT_DSW_SPEED_FAST | THEPIT_DSW_TIME_LONG | \
                          THEPIT_DSW_UPRIGHT | THEPIT_DSW_3_LIVES)

#endif
