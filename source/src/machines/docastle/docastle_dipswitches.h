#ifndef DOCASTLE_DIPSWITCHES_H
#define DOCASTLE_DIPSWITCHES_H

// MAME INPUT_PORTS_START(docastle), read through the TMS1025 input mux.
// DSW1 (SWA):
// bits 0-1 difficulty, 3 = 1 (beginner) .. 0 = 4 (advanced)
// bit 2 rack test, 1 = off
// bit 3 advance level on getting diamond, 1 = off
// bit 4 difficulty of EXTRA, 1 = easy
// bit 5 cabinet, 0 = upright
// bits 6-7 lives, 0 = 2, 3 = 3, 2 = 4, 1 = 5
#define DOCASTLE_DSW1 0xdf

// DSW2 (SWB): bits 0-3 coin B, bits 4-7 coin A, 0xf = 1 coin 1 credit
#define DOCASTLE_DSW2 0xff

#endif
