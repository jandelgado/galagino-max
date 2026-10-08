#include "hero.h"

#ifdef ENABLE_HERO

#include "hero_data.h"

using namespace hero_video;

void hero::reset() {
  machineBase::reset();
  level = 0;
  screen = 0;
  frame = 0;
  prev_keys = 0;
  game_started = 1; // no boot phase: wait for the display's 60 Hz from the start
  publish();
}

void hero::publish() {
  vblank.publish([this](VideoState &s) {
    s.room = hero_room::decode(level, screen);
    s.level = level;
    s.screen = screen;
    s.frame = frame;
  });
}

void hero::run_frame(void) {
  const unsigned int keys = input->buttons_get();
  const unsigned int pressed = keys & ~prev_keys;
  prev_keys = keys;
  if (pressed & BUTTON_RIGHT) { hero_room::step(level, screen, 1); }
  if (pressed & BUTTON_LEFT)  { hero_room::step(level, screen, -1); }
  if (pressed & BUTTON_UP) {
    level = (level + 1) % hero_data::LEVELS;
    screen = 0;
  }
  if (pressed & BUTTON_DOWN) {
    level = (level + hero_data::LEVELS - 1) % hero_data::LEVELS;
    screen = 0;
  }
  frame++;
  publish();
}

void hero::prepare_frame(void) {
  vblank.read(video.v);
  prepare(video);
}

void hero::render_row(short row) {
  uint8_t line[SRC_W];
  for (int sr = 0; sr < 8; sr++) {
    unsigned short *dst = frame_buffer + sr * OUT_W;
    const int out = row * 8 + sr;
    if (out >= OUT_H) {
      memset(dst, 0, OUT_W * sizeof(unsigned short));
      continue;
    }
    composeLine(video, sourceLine(out), line);
    scaleLine(line, dst);
  }
}

#ifdef LED_PIN
// level 1 wall colours, dark to light and back
void hero::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      CRGB(0x612300), CRGB(0x854a00), CRGB(0xa86e1a), CRGB(0xffd68b),
      CRGB(0xa86e1a), CRGB(0x854a00), CRGB(0x612300)};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif

#endif // ENABLE_HERO
