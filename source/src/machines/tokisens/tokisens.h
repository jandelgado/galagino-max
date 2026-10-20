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

  // 8 VRAM pages of 2 KB, banked into e000-efff (system1 VRAM_8_PAGES)
  static constexpr uint32_t ARENA_EXTRA[] = { 16384 };

protected:
  uint8_t dswA() override { return TOKISENS_OPTIONS; }
  uint8_t dswB() override { return TOKISENS_COINAGE; }
};

#endif
