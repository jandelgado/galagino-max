#ifndef TOKISENS_H
#define TOKISENS_H

#include "../system1/system1.h"
#include "tokisens_arena_plan.h"
#include "tokisens_dipswitches.h"

// Toki no Senshi - Chrono Soldier (Sega 1987), System 2 board, MC-8123
class tokisens : public system1 {
public:
  tokisens();

  signed char machineType() override { return MCH_TOKISENS; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

  // system1 VRAM_PAGED: 8 VRAM pages, published and render bg page copies
  static constexpr uint32_t ARENA_EXTRA[] = { 16384, 8192, 8192 };
  static_assert(ARENA_EXTRA[0] == PAGED_VRAM_BYTES && ARENA_EXTRA[1] == PAGED_SNAP_BYTES &&
                    ARENA_EXTRA[2] == PAGED_SNAP_BYTES, "update ARENA_EXTRA");
  static_assert(arenaExtrasKey(ARENA_EXTRA, sizeof(ARENA_EXTRA) / sizeof(uint32_t), false) ==
                    tokisens_arena_extras_key, "ARENA_EXTRA changed: rerun romconv");

protected:
  uint8_t dswA() override { return TOKISENS_OPTIONS; }
  uint8_t dswB() override { return TOKISENS_COINAGE; }
};

#endif
