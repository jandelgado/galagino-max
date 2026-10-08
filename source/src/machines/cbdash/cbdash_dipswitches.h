#ifndef CBDASH_DIPSWITCHES_H
#define CBDASH_DIPSWITCHES_H

// DSW1 (e300), MAME INPUT_PORTS_START( decocass ):
// b0-1  coin A: 11 = 1 coin 1 credit
// b2-3  coin B: 11 = 1 coin 1 credit
// b4-5  type of tape: 11 = MD (small), read by the BIOS only
// b6    cabinet: 0 = upright
// b7    VBLANK, set by the machine
#define CBDASH_DSW1 0x3f

// DSW2 (e301), MAME INPUT_PORTS_START( cbdash ):
// b0    lives: 1 = 3, 0 = 5
// b1-2  bonus life: 11 = 20000, 10 = 30000, 01 = 40000, 00 = none
// b3-4  difficulty: 11 = normal, 10 = hard, 01 = harder, 00 = hardest
// b5-7  country code: 111 = A, "don't change"
#define CBDASH_DSW2 0xff

#endif
