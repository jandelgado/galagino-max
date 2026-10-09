#include "hero.h"

#ifdef ENABLE_HERO

#include "hero_data.h"

using namespace hero_video;

void hero::reset() {
  machineBase::reset();
  hero_game::newGame(game, 0);
#ifdef HERO_ROOM_VIEWER
  hero_game::viewRoom(game, 0, 0);
  game.view.hero_page = NO_HERO;
#endif
  prev_keys = 0;
  game_started = 1; // no boot phase: wait for the display's 60 Hz from the start
  publish();
}

void hero::publish() {
  vblank.publish([this](VideoState &s) { s = game.view; });
}

// LEFT/RIGHT previous/next screen, UP/DOWN next/previous level
void hero::runViewer(uint32_t pressed) {
  uint8_t level = game.level, screen = game.screen;
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
  hero_game::viewRoom(game, level, screen);
  game.view.hero_page = NO_HERO;
}

void hero::run_frame(void) {
  uint32_t keys = input->buttons_get();
  if (keys & BUTTON_EXTRA) { keys &= ~(BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT); } // volume
#ifdef HERO_ROOM_VIEWER
  runViewer(keys & ~prev_keys);
#else
  hero_game::Input in;
  in.up = (keys & BUTTON_UP) != 0;
  in.down = (keys & BUTTON_DOWN) != 0;
  in.left = (keys & BUTTON_LEFT) != 0;
  in.right = (keys & BUTTON_RIGHT) != 0;
  in.fire = (keys & BUTTON_FIRE) != 0;
  hero_game::tick(game, in);
  // until the attract mode exists: FIRE after the game over starts game 1
  if (game.over && in.fire && !(prev_keys & BUTTON_FIRE)) { hero_game::newGame(game, 0); }
#endif
  prev_keys = keys;
  publish();
}

void hero::prepare_frame(void) {
  vblank.read(video.v);
  prepare(video);
}

void hero::render_row(short row) {
  uint8_t line[SRC_W];
  for (int sr = 0; sr < 8; sr++) {
    composeLine(video, sourceLine(row * 8 + sr), line);
    scaleLine(line, frame_buffer + sr * OUT_W);
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
