#ifndef GORF_DIPSWITCHES_H
#define GORF_DIPSWITCHES_H

// MAME INPUT_PORTS_START(gorf), values as read.

// P1HANDLE b6-7 jumpers: b6 cabinet upright (1), b7 speech on (1).
// b0-5 are coin/start/service/tilt, active low, set by the machine.
#define GORF_P1_JUMPERS 0xc0

// P2HANDLE: cocktail stick + fire, active low, not pressed; b5-7 unused (1)
#define GORF_P2 0xff

// P4HANDLE (S1), b7 demo sounds is set by the machine:
// b0    coin A 1C 1C (1)
// b1-2  coin B 1C 1C (11)
// b3    language English (1)
// b4    lives per credit 3 (0)
// b5    bonus life at mission 5 (1)
// b6    free play off (1)
#define GORF_DSW 0x6f
#define GORF_DSW_DEMO_SOUNDS_ON 0x80

#endif
