#include "emulation.h"
#include "hud.h"

#ifdef DEBUG_TIMING_FPS_HUD

static const uint16_t HUD_COLOR = 0xffff; // white, byte order independent
static const uint8_t GLYPH_WIDTH = 5;
static const uint8_t GLYPH_TOP_BIT = 0x40;
static const uint8_t GLYPH_ADVANCE = GLYPH_WIDTH + 1;
static const uint16_t FPS_MAX = 999;        // 3 digits
static const uint8_t ROW_FIRST = 0;
static const uint8_t ROW_LAST = 35;
static const uint8_t ROW_LINES = 8;

// font 5x7 digits 0-9
static const uint8_t hud_digits[10][GLYPH_WIDTH] = {
  { 0x3e, 0x45, 0x49, 0x51, 0x3e }, // 0
  { 0x00, 0x21, 0x7f, 0x01, 0x00 }, // 1
  { 0x23, 0x45, 0x49, 0x49, 0x31 }, // 2
  { 0x42, 0x41, 0x49, 0x59, 0x66 }, // 3
  { 0x0c, 0x14, 0x24, 0x7f, 0x04 }, // 4
  { 0x72, 0x51, 0x51, 0x51, 0x4e }, // 5
  { 0x1e, 0x29, 0x49, 0x49, 0x46 }, // 6
  { 0x40, 0x47, 0x48, 0x50, 0x60 }, // 7
  { 0x36, 0x49, 0x49, 0x49, 0x36 }, // 8
  { 0x31, 0x49, 0x49, 0x4a, 0x3c }, // 9
};

// one 8 line row of the frame buffer, addressed in physical (post flip)
// orientation: x=0,y=0 is the top-left pixel as seen on screen
struct HudRow {
  uint16_t *buffer;
  uint16_t width;
  bool mirror_h;
  bool mirror_v;

  // draw digit `c` with its top-left at x,y. other chars are skipped
  void render_char(uint16_t x, uint8_t y, char c) const {
    if (c < '0' || c > '9') {
      return;
    }

    const int32_t step_x = mirror_h ? -1 : 1;
    const int32_t step_y = mirror_v ? -int32_t(width) : int32_t(width);
    uint16_t *dst = buffer
                  + (mirror_v ? ROW_LINES - 1 - y : y) * width
                  + (mirror_h ? width - 1 - x : x);

    const uint8_t *glyph = hud_digits[c - '0'];
    for (uint8_t col = 0; col < GLYPH_WIDTH; col++) {
      uint16_t *p = dst + col * step_x;
      for (uint8_t bit = GLYPH_TOP_BIT; bit; bit >>= 1, p += step_y) {
        if (glyph[col] & bit) {
          *p = HUD_COLOR;
        }
      }
    }
  }
};

void hud_render_fps(uint16_t *frame_buffer, uint16_t width, uint8_t row, uint8_t flip) {
  // undo the machine flip so the HUD stays at the physical bottom, upright.
  // flip Y alone mirrors both axes, flip X then mirrors columns back again.
  const bool mirror_v = flip & HUD_FLIP_Y;
  const bool mirror_h = mirror_v != bool(flip & HUD_FLIP_X);

  if (row != (mirror_v ? ROW_FIRST : ROW_LAST)) {
    return;
  }

  uint16_t fps = emulation_fps;
  if (fps > FPS_MAX) {
    fps = FPS_MAX;
  }

  // least significant digit first
  char digits[3];
  uint8_t n = 0;
  do {
    digits[n++] = '0' + fps % 10;
    fps /= 10;
  } while (fps);

  // horizontally centered, line 1 (one pixel margin)
  const HudRow hud = { frame_buffer, width, mirror_h, mirror_v };
  const uint16_t text_width = n * GLYPH_ADVANCE - 1;
  uint16_t x = (width - text_width) / 2;
  while (n--) {
    hud.render_char(x, 1, digits[n]);
    x += GLYPH_ADVANCE;
  }
}

#endif
