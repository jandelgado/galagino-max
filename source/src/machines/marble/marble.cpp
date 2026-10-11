#include "marble.h"

#ifdef ENABLE_MARBLE

using namespace marble_video;

void marble::reset() {
  machineBase::reset();
  view = {PAN / 2, 0, 0};
  prev_keys = 0;
  game_started = 1;  // no boot phase
  vblank.publish([this](VideoState &s) { s = view; });
}

void marble::start() {
  store.init();  // guarded, start() runs twice
}

void marble::run_frame(void) {
  const uint32_t keys = input->buttons_get();
  const uint32_t pressed = keys & ~prev_keys;
  prev_keys = keys;
  if (keys & BUTTON_EXTRA) { return; }  // EXTRA + UP/DOWN is volume
  if (keys & BUTTON_UP)    { view.sy -= 4; }
  if (keys & BUTTON_DOWN)  { view.sy += 4; }
  if (keys & BUTTON_LEFT)  { view.sx -= 2; }
  if (keys & BUTTON_RIGHT) { view.sx += 2; }
  if (pressed & BUTTON_FIRE) {
    view = {PAN / 2, 0, (uint8_t)((view.level + 1) % marble_level::LEVELS)};
  }
  // clamp needs the level height; the emulation core only knows the static row counts
  static const int16_t heights[marble_level::LEVELS] = {600, 1160, 1080, 1024, 1136, 864};
  if (view.sx < 0) { view.sx = 0; }
  if (view.sx > PAN) { view.sx = PAN; }
  if (view.sy < 0) { view.sy = 0; }
  if (view.sy > heights[view.level] - OUT_H) { view.sy = heights[view.level] - OUT_H; }
  vblank.publish([this](VideoState &s) { s = view; });
}

void marble::prepare_frame(void) {
  vblank.read(video);
  if (video.level != loaded) {  // video core owns the level buffers
#ifdef ARDUINO
    uint32_t t0 = millis();
#endif
    level = &store.load(video.level);
#ifdef ARDUINO
    printf("marble: level %u loaded in %u ms\n", (unsigned)video.level, (unsigned)(millis() - t0));
#endif
    loaded = video.level;
  }
  clamp(video, *level);
}

void marble::render_row(short row) {
  renderRow(*level, video, row, frame_buffer);
}

#ifdef LED_PIN
void marble::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      CRGB(0x4d3b00), CRGB(0x997600), CRGB(0xc8ab2e), CRGB(0xf4e05d),
      CRGB(0xc8ab2e), CRGB(0x997600), CRGB(0x4d3b00)};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif

#endif // ENABLE_MARBLE
