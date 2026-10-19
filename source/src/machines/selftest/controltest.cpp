#include "controltest.h"

#ifdef BOOT_SELFTEST
#include "../../emulation/hud.h"

static constexpr uint8_t ROW_LINES = 8;
static constexpr uint16_t LINE_SPACING = 16;
static constexpr uint16_t TITLE_X = 52;  // centered
static constexpr uint16_t TITLE_Y = 32;
static constexpr uint16_t LABEL_X = 16;
static constexpr uint16_t STATE_X = LABEL_X + 9 * HUD_GLYPH_SIZE;
static constexpr uint16_t ROWS_Y = 56;
static constexpr uint16_t HINT_Y = 272;

static constexpr uint16_t NONE = 0;

// physical inputs as buttons_get() reports them, see Input::buttons_get().
// without coin pin the START button arrives as BUTTON_EXTRA, BUTTON_START
// and BUTTON_COIN are then only virtual pulses
const ControlTest::Row ControlTest::ROWS[] = {
  { "UP", BUTTON_UP },
  { "DOWN", BUTTON_DOWN },
  { "LEFT", BUTTON_LEFT },
  { "RIGHT", BUTTON_RIGHT },
#if defined(GALAGINO_CONTROLLER)
  { "FIRE", BUTTON_FIRE },
  { "START", BUTTON_START },
  { "COIN", BUTTON_COIN },
  { "A", BUTTON_A },
  { "B", BUTTON_B },
  { "X", BUTTON_X },
  { "Y", BUTTON_Y },
  { "L1", BUTTON_L1 },
  { "R1", BUTTON_R1 },
#elif defined(NUNCHUCK_INPUT) && defined(BTN_COIN_PIN)
  { "FIRE Z", BUTTON_FIRE },
  { "C", BUTTON_EXTRA },
  { "START", BUTTON_START },
  { "COIN", BUTTON_COIN },
#elif defined(NUNCHUCK_INPUT)
  { "FIRE Z", BUTTON_FIRE },
  { "START C", BUTTON_EXTRA },
  { "COIN", NONE },
#elif defined(BTN_COIN_PIN)
  { "FIRE", BUTTON_FIRE },
  { "START", BUTTON_START },
  { "COIN", BUTTON_COIN },
#else
  { "FIRE", BUTTON_FIRE },
  { "START", BUTTON_EXTRA },
  { "COIN", NONE },
#endif
};
const uint8_t ControlTest::ROW_COUNT = sizeof(ROWS) / sizeof(ROWS[0]);

// the key Input resets on: MENU, else BUTTON_EXTRA held 3 s
#if defined(GALAGINO_CONTROLLER)
static constexpr const char *EXIT_HINT = "PRESS MENU TO EXIT";
#elif defined(NUNCHUCK_INPUT)
static constexpr const char *EXIT_HINT = "HOLD C 3S TO EXIT";
#elif defined(BTN_COIN_PIN)
static constexpr const char *EXIT_HINT = "HOLD COIN 3S TO EXIT";
#else
static constexpr const char *EXIT_HINT = "HOLD START 3S TO EXIT";
#endif

#ifdef NUNCHUCK_INPUT
// connected() is a real I2C read, too slow for every frame
static constexpr uint32_t NUNCHUCK_POLL_MS = 1000;
#endif

ControlTest::ControlTest(Input *input, Exit exit) : input(input), exit(exit) {}

void ControlTest::frame(unsigned int keys, uint32_t ms) {
  held = keys;
  seen |= keys;

#ifdef NUNCHUCK_INPUT
  if (!nunchuck_polled || ms - nunchuck_ms >= NUNCHUCK_POLL_MS) {
    nunchuck_polled = true;
    nunchuck_ms = ms;
    nunchuck_ok = input->nunchuckConnected();
  }
#else
  (void)ms;
#endif
}

int8_t ControlTest::heldRow() const {
  for (uint8_t i = 0; i < ROW_COUNT; i++) {
    if (held & ROWS[i].mask) {
      return i;
    }
  }
  return -1;
}

// one text line, drawn only in the strip row holding it
static void text(uint16_t *frame_buffer, uint16_t width, uint8_t row, uint16_t x, uint16_t y, uint16_t color,
                 const char *txt) {
  static_assert(HUD_GLYPH_SIZE == ROW_LINES, "a text line must fill exactly one strip row");
  if (y / ROW_LINES != row) {
    return;
  }
  hud_text(frame_buffer, width, row, HUD_FLIP_NONE, x, y, color, txt);
}

void ControlTest::draw(uint16_t *frame_buffer, uint16_t width, uint8_t row) const {
  text(frame_buffer, width, row, TITLE_X, TITLE_Y, HUD_WHITE, "CONTROLLER TEST");

  for (uint8_t i = 0; i < ROW_COUNT; i++) {
    const Row &r = ROWS[i];
    const uint16_t y = ROWS_Y + i * LINE_SPACING;
    text(frame_buffer, width, row, LABEL_X, y, HUD_WHITE, r.label);

    if (r.mask == NONE) {
      text(frame_buffer, width, row, STATE_X, y, HUD_GRAY, "NONE");
    }
    else if (held & r.mask) {
      text(frame_buffer, width, row, STATE_X, y, HUD_RED, "ON");
    }
    else if (seen & r.mask) {
      text(frame_buffer, width, row, STATE_X, y, HUD_WHITE, "OK");
    }
    else {
      text(frame_buffer, width, row, STATE_X, y, HUD_GRAY, "--");
    }
  }

#ifdef NUNCHUCK_INPUT
  const uint16_t nunchuck_y = ROWS_Y + (ROW_COUNT + 1) * LINE_SPACING;
  text(frame_buffer, width, row, LABEL_X, nunchuck_y, HUD_WHITE, "NUNCHUCK");
  text(frame_buffer, width, row, STATE_X, nunchuck_y, nunchuck_ok ? HUD_WHITE : HUD_RED, nunchuck_ok ? "OK" : "NG");
#endif

  if (exit == EXIT_RESET) {
    text(frame_buffer, width, row, LABEL_X, HINT_Y, HUD_GRAY, EXIT_HINT);
  }
}
#endif
