#ifndef PHOZON_DIPSWITCHES_H
#define PHOZON_DIPSWITCHES_H

// all active low, MAME defaults (namco/mappy.cpp INPUT_PORTS phozon)

// DSW0 (56XX pins 30-33): bit2 cabinet (1 = upright), bit3 service (1 = off)
#define PHOZON_DSW0  0x0f

// DSW1 (56XX pins 22-29):
// bit0    unknown (1 = off)
// bit1-3  difficulty (0x0e = rank 0 ... 0x00 = rank 7)
// bit4    level select cheat (1 = off)
// bit5    stop mode cheat (1 = off)
// bit6-7  coin B (0xc0 = 1 coin 1 credit)
#define PHOZON_DSW1  0xff

// DSW2 (via LS157 dipmux, nibbles interleaved):
// bit0,4      lives (0x11 = 3)
// bit1,5,6    bonus life (0x62 = 30k & 100k)
// bit2,3,7    coin A (0x8c = 1 coin 1 credit)
#define PHOZON_DSW2  0xff

#endif
