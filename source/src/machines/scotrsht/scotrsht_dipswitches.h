#ifndef SCOTRSHT_DIPSWITCHES_H
#define SCOTRSHT_DIPSWITCHES_H

// MAME konami/scotrsht.cpp INPUT_PORTS_START( scotrsht ), active low.
// MAME reads 3303 = ff, 3100 = 7e, 3200 = 0f.

// DSW1 (0x3303): coinage, 0xff = 1 coin 1 credit on both slots
#define SCOTRSHT_DSW1 0xff

// DSW2 (0x3100)
#define SCOTRSHT_DSW2_LIVES_3        0x02  // b0-1: 03 = 2, 02 = 3, 01 = 4, 00 = 5
#define SCOTRSHT_DSW2_MUST_BE_OFF    0x04  // b2: 1 (off), else P1 controls only
#define SCOTRSHT_DSW2_BONUS_30K      0x08  // b3: 1 = 30k 110k 80k+, 0 = 40k 120k 90k+
#define SCOTRSHT_DSW2_EASY           0x30  // b4-5: 30 easy, 20 normal, 10 hard, 00 hardest
#define SCOTRSHT_DSW2_UNUSED         0x40  // b6
#define SCOTRSHT_DSW2_DEMO_SOUND_OFF 0x80  // b7: 0 = demo sounds on
#define SCOTRSHT_DSW2 (SCOTRSHT_DSW2_LIVES_3 | SCOTRSHT_DSW2_MUST_BE_OFF | \
                       SCOTRSHT_DSW2_BONUS_30K | SCOTRSHT_DSW2_EASY | SCOTRSHT_DSW2_UNUSED)

// DSW3 (0x3200): b0 flip off, b1 must be off (1), b2-3 unused, b4-7 not wired (0)
#define SCOTRSHT_DSW3 0x0f

#endif
