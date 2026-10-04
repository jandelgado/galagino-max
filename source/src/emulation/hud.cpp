#include "emulation.h"
#include "hud.h"

#if defined(DEBUG_TIMING_FPS_HUD) || defined(BOOT_SELFTEST)

#include "hud_font.h"

static constexpr uint8_t ROWS = 36;
static constexpr uint8_t ROW_LINES = 8;
static constexpr uint8_t FONT_LETTERS = 10;
static constexpr uint8_t FONT_DASH = 36;
static constexpr uint8_t FONT_DOT = 37;

static const uint8_t *hud_glyph(char c) {
  if (c >= '0' && c <= '9') {
    return hud_font[c - '0'];
  }
  if (c >= 'A' && c <= 'Z') {
    return hud_font[c - 'A' + FONT_LETTERS];
  }
  if (c >= 'a' && c <= 'z') {
    return hud_font[c - 'a' + FONT_LETTERS];
  }
  if (c == '-') {
    return hud_font[FONT_DASH];
  }
  if (c == '.') {
    return hud_font[FONT_DOT];
  }
  return nullptr;
}

void hud_text(uint16_t *frame_buffer, uint16_t width, uint8_t row, uint8_t flip,
              uint16_t x, uint16_t y, uint16_t color, const char *txt) {
  // undo the machine flip so the text stays upright at its physical spot.
  // flip Y alone mirrors both axes, flip X then mirrors columns back again.
  const bool mirror_v = flip & HUD_FLIP_Y;
  const bool mirror_h = mirror_v != bool(flip & HUD_FLIP_X);

  // physical lines covered by both this strip and the text
  const uint16_t top = (mirror_v ? ROWS - 1 - row : row) * ROW_LINES;
  const uint16_t first = y > top ? y : top;
  const uint16_t end = (y + HUD_GLYPH_SIZE < top + ROW_LINES) ? y + HUD_GLYPH_SIZE : top + ROW_LINES;
  if (first >= end) {
    return;
  }

  const int32_t step_x = mirror_h ? -1 : 1;
  for (; *txt && x + HUD_GLYPH_SIZE <= width; txt++, x += HUD_GLYPH_SIZE) {
    const uint8_t *glyph = hud_glyph(*txt);
    if (!glyph) {
      continue;
    }

    for (uint16_t line = first; line < end; line++) {
      const uint8_t strip_line = line - top;
      uint16_t *p = frame_buffer
                  + (mirror_v ? ROW_LINES - 1 - strip_line : strip_line) * width
                  + (mirror_h ? width - 1 - x : x);
      for (uint8_t bits = glyph[line - y]; bits; bits <<= 1, p += step_x) {
        if (bits & 0x80) {
          *p = color;
        }
      }
    }
  }
}

#endif

#ifdef DEBUG_TIMING_FPS_HUD

static constexpr uint16_t FPS_MAX = 999;        // 3 digits
// bottom of the game area, 1 px left for the shadow
static constexpr uint16_t FPS_Y = ROWS * ROW_LINES - HUD_GLYPH_SIZE - 1;

void hud_render_fps(uint16_t *frame_buffer, uint16_t width, uint8_t row, uint8_t flip) {
  uint16_t fps = emulation_fps;
  if (fps > FPS_MAX) {
    fps = FPS_MAX;
  }

  // filled right to left
  char digits[4];
  uint8_t n = 3;
  digits[n] = 0;
  do {
    digits[--n] = '0' + fps % 10;
    fps /= 10;
  } while (fps);

  const uint16_t text_width = (3 - n) * HUD_GLYPH_SIZE;
  const uint16_t x = (width - text_width) / 2;
  hud_text(frame_buffer, width, row, flip, x + 1, FPS_Y + 1, HUD_GRAY, digits + n);
  hud_text(frame_buffer, width, row, flip, x, FPS_Y, HUD_WHITE, digits + n);
}

#endif
