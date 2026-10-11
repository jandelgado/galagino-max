#ifndef SPACDUEL_INPUTS_H
#define SPACDUEL_INPUTS_H

#include <stdint.h>
#include "spacduel_dipswitches.h"

// Space Duel reads its controls through 0x0900-0x0907, two inputs per
// address in bits 7 and 6, active high (MAME spacduel_IN3_r). Player 2
// never presses anything; upright cabinet.

// Select state of the game (RAM 0x03ee bit 7): after power up START is
// ignored until Select was pressed once (ROM 0x42a8), which picks the game
// type (1 player, set at 0x68e5)
enum SdSelect : uint8_t { SD_SELECTED, SD_SELECT_PENDING };

enum : uint8_t { SD_B7 = 0x80, SD_B6 = 0x40 };

static inline uint8_t sd_bits(bool b7, bool b6) {
  return (b7 ? SD_B7 : 0) | (b6 ? SD_B6 : 0);
}

// needs BUTTON_* from input.h; k = buttons, offset = address & 7
static inline uint8_t spacduel_in1(uint8_t offset, unsigned int k, SdSelect select) {
  switch (offset) {
    case 0: return sd_bits(k & (BUTTON_DOWN | BUTTON_B), k & (BUTTON_FIRE | BUTTON_A)); // shield, fire
    case 2: return sd_bits(k & BUTTON_LEFT, k & BUTTON_RIGHT);
    case 4: return sd_bits(k & BUTTON_UP, k & BUTTON_START); // thrust, start
    case 5: return sd_bits(false, !(SPACDUEL_DSW2 & 0x01)); // P2 thrust, charge by game
    // Select with START while pending, so one START press starts a game
    case 6: return sd_bits(select == SD_SELECT_PENDING && (k & BUTTON_START), !(SPACDUEL_DSW2 & 0x02));
    case 7: return sd_bits(false, !(SPACDUEL_DSW2 & 0x04)); // cabinet, 1 player game only
  }
  return 0; // player 2
}

#endif
