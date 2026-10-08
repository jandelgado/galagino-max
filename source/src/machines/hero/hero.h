#ifndef HERO_H
#define HERO_H

#include "../machineBase.h"

#ifdef ENABLE_HERO

#include "hero_logo.h"
#include "hero_video.h"
#include "../../emulation/seqlock.h"

// H.E.R.O. (Activision 1984, Atari 2600). Not emulated: levels and graphics
// come from the ROM, the game is reimplemented (HERO.md). No CPU, run_frame()
// is one game tick at the display's 60 Hz.
// Room viewer for now: LEFT/RIGHT previous/next screen, UP/DOWN next/previous
// level.
class hero : public machineBase
{
public:
  signed char machineType() override { return MCH_HERO; }
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
  void publish();

  Seqlock<hero_video::VideoState> vblank;
  hero_video::Frame video; // render copy, video core only

  uint8_t level = 0;
  uint8_t screen = 0;
  uint8_t frame = 0;       // $81
  uint32_t prev_keys = 0;
};

#endif // ENABLE_HERO
#endif // HERO_H
