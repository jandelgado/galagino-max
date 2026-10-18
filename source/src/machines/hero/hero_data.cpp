#include "../../catalog.h"
#ifdef GAME_HERO

#include "hero_data.h"
#include "hero_rom.h"

namespace hero_data {

uint8_t b0(uint16_t addr) {
  return addr < HERO_B0_GFX_FIRST ? hero_b0_level[addr - HERO_B0_LEVEL_FIRST]
                                  : hero_b0_gfx[addr - HERO_B0_GFX_FIRST];
}

uint8_t b1(uint16_t addr) { return hero_b1[addr - HERO_B1_FIRST]; }

} // namespace hero_data

#endif // GAME_HERO
