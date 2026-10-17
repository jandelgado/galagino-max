#ifndef DEFENDER_H
#define DEFENDER_H

#include "../williams/williams.h"
#include "../williams/defender_board.h"
#include "defender_rom.h"
#include "defender_logo.h"

// Defender (Williams 1980), MAME williams/williams.cpp, set defender.
class defender : public williams {
public:
  // Arena::alloc besides planned assets, read by romconv (arena_plan.py):
  // williams_rgb (256 colors, base ctor), board RAM and 2 scanout buffers (hw, any order)
  static constexpr uint32_t ARENA_EXTRA[] = { 512, 49152, 35040, 35040 };
  static constexpr bool ARENA_EXTRA_ANY_ORDER = true;  // hw(...) arguments
  static_assert(ARENA_EXTRA[0] == 256 * sizeof(uint16_t) &&
                ARENA_EXTRA[1] == WilliamsBoard::RAM_SIZE && ARENA_EXTRA[2] == Scanout::BUF_BYTES &&
                ARENA_EXTRA[3] == Scanout::BUF_BYTES, "update ARENA_EXTRA");

private:
  // ROM assets: mode and Arena block from romconv (<game>_arena_plan.h)
  defender_rom_main_asset defender_rom_main;
  defender_rom_banked_asset defender_rom_banked;
  defender_rom_sound_asset defender_rom_sound;
  const uint8_t *romMain, *romBanked, *romSound;
  DefenderBoard hw;

public:
  defender();
  signed char machineType() override { return MCH_DEFENDER; }
  static const Logo &logo() { return defender_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif
};

#endif
