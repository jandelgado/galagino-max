#ifndef BERZERK_DIPSWITCHES_H
#define BERZERK_DIPSWITCHES_H

// MAME INPUT_PORTS_START(berzerk), values as read; unused bits are
// active low and read 1.

// F2: b0-1 color test off (00), b2-5 unused, b6-7 bonus life 5000 and 10000 (11)
#define BERZERK_DIP_F2 0xfc

// F3: b0 input test off, b1 crosshair off, b2-5 unused, b6-7 English (00)
#define BERZERK_DIP_F3 0x3c

// F4/F5/F6: coin 1/2/3, b0-3 1 coin 1 credit (0000), b4-7 unused
#define BERZERK_DIP_F4 0xf0
#define BERZERK_DIP_F5 0xf0
#define BERZERK_DIP_F6 0xf0

// SW2: b0 free game button (active high) off, b1-6 unused, b7 bookkeeping off
#define BERZERK_SW2 0x7e

// P2: no player 2 inputs, b7 cabinet upright (1)
#define BERZERK_P2 0xff

#endif
