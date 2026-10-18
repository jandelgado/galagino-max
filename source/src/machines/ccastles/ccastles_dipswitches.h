#ifndef CCASTLES_DIPSWITCHES_H
#define CCASTLES_DIPSWITCHES_H

// IN1 (POKEY 2 ALLPOT), MAME INPUT_PORTS_START( ccastles )
// b0-2 unknown (option switches at 9A08): 1 = off
// b3 1P start cocktail, b4 2P start cocktail: 1 = released
// b5 cabinet: 0 = upright
// b6-7 unused: 0
#define CCASTLES_IN1 0x1f

#endif
