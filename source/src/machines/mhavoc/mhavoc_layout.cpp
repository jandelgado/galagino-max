#include "mhavoc_layout.h"
#include "../../emulation/avg.h"

namespace mhavoc_layout {

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t rgb = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
  return (rgb >> 8) | (rgb << 8);
}

// MAME mhavoc_strobe3: inverted color RAM bits, red = bit 3 * 0xcb + bit 2
// * 0x34, green bit 1, blue bit 0 (0xcb each). Levels 1..7 ramp from a
// floor like tempest_layout so dim lines stay visible on the LCD.
void palette(uint16_t *pal) {
  static const uint8_t LEVELS = avg::TP_LEVEL_MASK + 1;
  static_assert(PALETTE_SIZE == 16 * LEVELS, "palette covers every color RAM value and level");
  for (uint8_t i = 0; i < PALETTE_SIZE; i++) {
    const uint8_t value = ~(i >> avg::TP_COLOR_SHIFT), level = i & avg::TP_LEVEL_MASK;
    const uint16_t ramp = level ? 80 + level * 25 : 0;
    const uint16_t gain = ramp < 255 ? ramp : 255;

    const uint16_t r = ((value & 8) ? 0xcb : 0) + ((value & 4) ? 0x34 : 0);
    const uint16_t g = (value & 2) ? 0xcb : 0;
    const uint16_t b = (value & 1) ? 0xcb : 0;
    pal[i] = rgb565(r * gain / 255, g * gain / 255, b * gain / 255);
  }
}

} // namespace mhavoc_layout
