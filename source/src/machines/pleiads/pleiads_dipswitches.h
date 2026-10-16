#ifndef PLEIADS_DIPSWITCHES_H
#define PLEIADS_DIPSWITCHES_H

// DSW0 @0x7800 (MAME default 0x60, bit 7 = vblank, active low):
//   b0-1 lives (0 = 3, 1 = 4, 2 = 5, 3 = 6)
//   b2-3 bonus life (0 = 3K 30K .. 3 = 6K 60K)
//   b4   coinage (0 = 1C/1C, 1 = 2C/1C)
//   b5   unknown (1 = off)
//   b6   demo sounds (1 = on)
#define PLEIADS_DSW0  0x60
#define PLEIADS_DSW0_DEMO_SOUNDS  0x40

#endif
