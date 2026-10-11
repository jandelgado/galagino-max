#ifndef MARBLE_H
#define MARBLE_H

#include "../machineBase.h"

#ifdef ENABLE_MARBLE

#include "marble_logo.h"
#include "marble_level.h"
#include "marble_video.h"
#include "../../emulation/seqlock.h"

// Marble Madness (Atari 1984). Not emulated: graphics, levels and music come from the
// ROM, the game is reimplemented (MARBLE.md). Phase 2: the marble rolls (stick as
// trackball, 30 Hz logic tick, camera, respawn); FIRE steps to the next race (debug).
class marble : public machineBase
{
public:
  signed char machineType() override { return MCH_MARBLE; }
  const int renderWidth()  override { return marble_video::OUT_W; }
  const int renderBuffer() override { return marble_video::OUT_W * 2 * 8; }

  void reset() override;
  void start() override;
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  static const Logo &logo(void) { return marble_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
#endif

private:
  void beginRace(uint8_t race);
  void logicTick(uint32_t keys);
  void publish(void);

  Seqlock<marble_video::VideoState> vblank;
  marble_video::VideoState view = {};   // emulation core
  marble_video::VideoState video = {};  // render copy, video core only
  // level buffers: written and read on the video core only (prepare_frame, render_row)
  marble_level::Store store;
  const marble_level::Level *level = nullptr;
  uint8_t loaded = 0xff;
  uint32_t prev_keys = 0;
  // emulation core: race state lives in the work RAM image, machineBase::memory
  uint8_t race = 0;
  uint8_t dead_ticks = 0;  // ticks since the marble broke or got lost
};

#endif // ENABLE_MARBLE
#endif // MARBLE_H
