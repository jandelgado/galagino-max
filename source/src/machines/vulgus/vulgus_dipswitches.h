#ifndef VULGUS_DIPSWITCHES_H
#define VULGUS_DIPSWITCHES_H

// DSW1 @0xC003 (MAME default 0xFF):
//   b0-1 lives (0x01=1, 0x02=2, 0x03=3, 0x00=5)
//   b2-4 coin B, b5-7 coin A (0x1C / 0xE0 = 1C/1C)
#define VULGUS_DSW1  0xFF

// DSW2 @0xC004 (MAME default 0x7F):
//   b2   demo music (1=on)
//   b3   demo sounds (1=on)
//   b4-6 bonus (0x70=20000 60000)
//   b7   cabinet (0=upright)
#define VULGUS_DSW2  0x7F
#define VULGUS_DSW2_DEMO_SOUNDS  0x0C

#endif
