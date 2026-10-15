#ifndef DIGDUG2_DIPSWITCHES_H
#define DIGDUG2_DIPSWITCHES_H

// DSW1 (56XX #1 port 22-29), active low, MAME default 0xff:
// bit0 service mode (1=off), bit1 lives (1=3, 0=5),
// bit2-3 coinage (0x0c=1C1C), bit4-5 bonus life (0x30=30k 80k and ...),
// bit6 level select (1=off), bit7 freeze (1=off)
#define DIGDUG2_DSW1  0xff

// DSW2 (multiplexed via LS157): unused
#define DIGDUG2_DSW2  0xff

// DSW0 (4 bit, 56XX #1 port 30-33), active low:
// bit0 button2 (jackhammer, cleared at runtime), bit1 button2 cocktail,
// bit2 cabinet (1=upright), bit3 service (1=off)
#define DIGDUG2_DSW0  0x0f

#endif
