#ifndef ROBOTRON_H
#define ROBOTRON_H

#include "../williams/williams.h"
#include "../williams/robotron_board.h"
#include "robotron_rom.h"
#include "robotron_logo.h"

// Robotron: 2084 (Williams / Vid Kidz 1982), MAME williams/williams.cpp,
// set robotron.
class robotron : public williams {
public:
  // Arena::alloc besides planned assets, read by romconv (romconv/romconv/plan/arena.py):
  // williams_rgb (256 colors, base ctor), board RAM and 2 scanout buffers (hw, any order)
  static constexpr uint32_t ARENA_EXTRA[] = { 512, 49152, 35040, 35040 };
  static constexpr bool ARENA_EXTRA_ANY_ORDER = true;  // hw(...) arguments
  static_assert(arenaExtrasKey(ARENA_EXTRA, sizeof(ARENA_EXTRA) / sizeof(uint32_t), ARENA_EXTRA_ANY_ORDER) ==
                robotron_arena_extras_key, "ARENA_EXTRA changed: rerun romconv");
  static_assert(ARENA_EXTRA[0] == 256 * sizeof(uint16_t) &&
                ARENA_EXTRA[1] == WilliamsBoard::RAM_SIZE && ARENA_EXTRA[2] == Scanout::BUF_BYTES &&
                ARENA_EXTRA[3] == Scanout::BUF_BYTES, "update ARENA_EXTRA");

private:
  // ROM assets: mode and Arena block from romconv (<game>_arena_plan.h)
  robotron_rom_main_asset robotron_rom_main;
  robotron_rom_sound_asset robotron_rom_sound;
  const uint8_t *romMain, *romSound;
  RobotronBoard hw;

public:
  robotron();
  uint8_t machineType() override { return MCH_ROBOTRON; }
  static const Logo &logo() { return robotron_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif
};

#endif
