#ifndef SINISTAR_H
#define SINISTAR_H

#include "../williams/williams.h"
#include "../williams/sinistar_board.h"
#include "sinistar_rom.h"
#include "sinistar_logo.h"

// Sinistar (Williams 1982), MAME williams/williams.cpp, set sinistar
// (rev. 3 upright). ROT270: shown upright, 292 native columns -> 288 rows.
class sinistar : public williams {
public:
  // Arena::alloc besides planned assets, read by romconv (arena_plan.py):
  // williams_rgb (256 colors, base ctor), board RAM and 2 scanout buffers (hw, any order)
  static constexpr uint32_t ARENA_EXTRA[] = { 512, 49152, 35040, 35040 };
  static constexpr bool ARENA_EXTRA_ANY_ORDER = true;  // hw(...) arguments
  static_assert(arenaExtrasKey(ARENA_EXTRA, sizeof(ARENA_EXTRA) / sizeof(uint32_t), ARENA_EXTRA_ANY_ORDER) ==
                sinistar_arena_extras_key, "ARENA_EXTRA changed: rerun romconv");
  static_assert(ARENA_EXTRA[0] == 256 * sizeof(uint16_t) &&
                ARENA_EXTRA[1] == WilliamsBoard::RAM_SIZE && ARENA_EXTRA[2] == Scanout::BUF_BYTES &&
                ARENA_EXTRA[3] == Scanout::BUF_BYTES, "update ARENA_EXTRA");

private:
  sinistar_rom_main_asset sinistar_rom_main;
  sinistar_rom_sound_asset sinistar_rom_sound;
  const uint8_t *romMain, *romSound;
  SinistarBoard hw;

public:
  sinistar();
  void reset() override;
  signed char machineType() override { return MCH_SINISTAR; }
  static const Logo &logo() { return sinistar_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static const uint16_t SRAM_OFS = 0x0800;  // memory[]: 0xd000-0xdfff
};

#endif
