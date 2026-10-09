#include "hero.h"

#ifdef ENABLE_HERO

#include "hero_data.h"
#include "../../emulation/pokey.h"

using namespace hero_video;

// TIA channel -> POKEY channel at the 64 kHz clock. TIA tones divide
// 31.4 kHz by AUDF + 1 and by 2 (AUDC 4) or 6 (AUDC $C); its 9 bit poly
// steps at that rate. POKEY divides 63.9 kHz by AUDF + 1, by 2 for tones.
static void tiaToPokey(const hero_game::Sound &s, uint8_t *regs) {
  static const uint8_t PURE = 0xa0;    // AUDC distortion: pure tone
  static const uint8_t POLY = 0x80;    // 17 bit poly, 9 bit with AUDCTL bit 7
  static const uint8_t AUDCTL_POLY9 = 0x80;
  static const uint8_t SKCTL_RUN = 3;
  for (int ch = 0; ch < 2; ch++) {
    const uint8_t f = (s.audf[ch] & 31) + 1;
    uint8_t dist = PURE;
    int32_t k = 0; // POKEY divider per TIA divider, x 100
    switch (s.audc[ch] & 15) {
    case 0x4: case 0x5: k = 204; break;
    case 0xc: case 0xd: k = 611; break;
    case 0x8: dist = POLY; k = 204; break;
    default: break; // AUDC 0: constant level, silent
    }
    regs[2 * ch] = k ? (f * k + 50) / 100 - 1 : 0;
    regs[2 * ch + 1] = k ? dist | (s.audv[ch] & 15) : 0;
  }
  regs[Pokey::REG_AUDCTL] = AUDCTL_POLY9;
  regs[Pokey::REG_SKCTL] = SKCTL_RUN;
}

void hero::reset() {
  machineBase::reset();
  hero_game::powerOn(game);
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

#ifdef HERO_ROOM_VIEWER
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
#endif

void hero::run_frame(void) {
  uint32_t keys = input->buttons_get();
  const bool start = (keys & BUTTON_EXTRA) != 0;
  if (start) { keys &= ~(BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT); } // volume
#ifdef HERO_ROOM_VIEWER
  runViewer(keys & ~prev_keys);
#else
  hero_game::Input in;
  in.up = (keys & BUTTON_UP) != 0;
  in.down = (keys & BUTTON_DOWN) != 0;
  in.left = (keys & BUTTON_LEFT) != 0;
  in.right = (keys & BUTTON_RIGHT) != 0;
  in.fire = (keys & BUTTON_FIRE) != 0;
  // console switches: START is RESET, UP/DOWN outside a game is SELECT
  in.start = start;
  in.select = game.over && (in.up || in.down);
  hero_game::tick(game, in);
  tiaToPokey(game.sound, soundregs);
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
