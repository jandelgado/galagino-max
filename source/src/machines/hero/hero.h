#ifndef HERO_H
#define HERO_H

#include "../machineBase.h"
#include "../../emulation/pokey.h"

#include "../../catalog.h"
#ifdef GAME_HERO

#include "hero_logo.h"
#include "hero_game.h"
#include "hero_video.h"
#include "../../emulation/seqlock.h"

// H.E.R.O. (Activision 1984, Atari 2600). Not emulated: levels and graphics
// come from the ROM, the game is reimplemented (HERO.md). No CPU, run_frame()
// is one game tick at the display's 60 Hz.
// HERO_ROOM_VIEWER: instead of the game, LEFT/RIGHT step screens, UP/DOWN
// levels.
class hero : public machineBase
{
public:
  signed char machineType() override { return MCH_HERO; }
  void renderAudio(int16_t *out, int n) override {
    Pokey::renderBank(pokey, soundregs, out, n);
    audioClampAll(out, n, AUDIO_MAX);
  }
  const int renderWidth()  override { return hero_video::OUT_W; }
  const int renderBuffer() override { return hero_video::OUT_W * 2 * 8; }

  void reset() override;
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  static const Logo &logo(void) { return hero_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
#endif

private:
#ifdef HERO_ROOM_VIEWER
  void runViewer(uint32_t pressed);
#endif
  void publish();

  Seqlock<hero_video::VideoState> vblank;
  hero_video::Frame video; // render copy, video core only

  hero_game::Game game;
  uint32_t prev_keys = 0;

private:
  Pokey pokey[1] = {Pokey(Pokey::CLOCKS_1789KHZ)};
};

#endif // GAME_HERO
#endif // HERO_H
