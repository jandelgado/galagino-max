#ifndef DOTRON_AIM_H
#define DOTRON_AIM_H

#include <stdint.h>
#include "../../emulation/input.h"

// Discs of Tron aims with a spinner dial plus aim up/down buttons; galagino
// has a stick and one button. The game already knows how to aim: in attract
// mode (demo flag 0xe506 set) ROM 0x5f65 points the aim target 0xe189 at
// the opponent (routine 0x6299) instead of reading the dial:
//
//   5f65: ld a,($e506) / or a / jr z,$5f99   ; player: dial + elevation
//   5f6b: ...demo: aim at target...
//
// Turning the "jr z" into two nops gives the player that auto-aim (MAME
// test: blind fire every 0.5 s scores 3300 vs 1400). The patch goes into
// the decompressed main ROM only after boot, so the power-up ROM checksum
// (0xbd9b, before the first interrupt) sees the original bytes. The demo
// path skips dial and elevation input, so the controller's manual aim keys
// (L1/R1 dial, X/Y elevation) restore the branch while held.
class DotronAim {
public:
  enum Phase { BOOTING, RUNNING };

  // rom: writable main ROM copy, patched in place
  void attach(uint8_t *rom) {
    code = rom;
    patched = false;
    known = rom[BRANCH] == JR_Z && rom[BRANCH + 1] == OFFSET;
  }

  // once per frame
  void update(unsigned int keys, Phase phase) {
    const bool manual = keys & (BUTTON_L1 | BUTTON_R1 | BUTTON_X | BUTTON_Y);
    if (phase == BOOTING || manual) {
      restore();
      return;
    }
    if (!known || patched) {
      return;
    }
    code[BRANCH] = NOP;
    code[BRANCH + 1] = NOP;
    patched = true;
  }

  void restore() {
    if (!patched) {
      return;
    }
    code[BRANCH] = JR_Z;
    code[BRANCH + 1] = OFFSET;
    patched = false;
  }

private:
  static const uint16_t BRANCH = 0x5f69;  // jr z,$5f99
  static const uint8_t JR_Z = 0x28, OFFSET = 0x2e, NOP = 0x00;

  uint8_t *code = nullptr;
  bool known = false;
  bool patched = false;
};

#endif
