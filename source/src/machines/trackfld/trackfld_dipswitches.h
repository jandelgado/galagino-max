#ifndef TRACKFLD_DIPSWITCHES_H
#define TRACKFLD_DIPSWITCHES_H

// MAME konami/trackfld.cpp INPUT_PORTS_START( trackfld ), active low

// DSW1 (0x1283): coinage, 0xff = 1 coin 1 credit on both slots
#define TRACKFLD_DSW1 0xff

// DSW2 (0x1200), MAME defaults
#define TRACKFLD_DSW2_LIVES_1        0x01  // b0: 1 = 1, 0 = 2
#define TRACKFLD_DSW2_CONTINUES      0x00  // b1: 0 = game continues after last event
#define TRACKFLD_DSW2_UPRIGHT        0x00  // b2: 0 upright, 1 cocktail
#define TRACKFLD_DSW2_NO_BONUS       0x08  // b3: 1 = none, 0 = 100000
#define TRACKFLD_DSW2_KEEP_RECORDS   0x10  // b4: 1 = don't erase world records
#define TRACKFLD_DSW2_NORMAL         0x40  // b5-6: 60 easy, 40 normal, 20 hard, 00 difficult
#define TRACKFLD_DSW2_DEMO_SOUND_OFF 0x80  // b7: 0 = demo sounds on
#define TRACKFLD_DSW2 (TRACKFLD_DSW2_LIVES_1 | TRACKFLD_DSW2_CONTINUES | TRACKFLD_DSW2_UPRIGHT | \
                       TRACKFLD_DSW2_NO_BONUS | TRACKFLD_DSW2_KEEP_RECORDS | TRACKFLD_DSW2_NORMAL)

#endif
