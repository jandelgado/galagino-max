#ifndef LLANDER_DIPSWITCHES_H
#define LLANDER_DIPSWITCHES_H

// DSW1 (MAME "llander" ports, rev 2 ROMs), read 2 bits per address
// through a LS153:
//   bits 0-1 right coin mech: 0 x1, 1 x4, 2 x5, 3 x6
//   bits 2-3 language: 0 English, 1 French, 2 Spanish, 3 German
//   bit 5 coinage: 0 normal, 1 free play
//   bits 4, 6-7 fuel units per coin: 0x00 450, 0x40 600, 0x80 750,
//     0xc0 900, 0x10 1100, 0x50 1300, 0x90 1550, 0xd0 1800
#define LLANDER_DSW 0x80 // English, 750 fuel per coin, coined, right coin x1

#endif
