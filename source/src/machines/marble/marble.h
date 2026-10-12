#ifndef MARBLE_H
#define MARBLE_H

#include "../machineBase.h"

#ifdef ENABLE_MARBLE

#include "marble_logo.h"
#include "marble_level.h"
#include "marble_video.h"
#include "../../emulation/seqlock.h"
#include "marble_mem.h"
#include "marble_flow.h"
#include "marble_hud.h"

// Marble Madness (Atari 1984). Not emulated: graphics, levels and music come from the
// ROM, the game is reimplemented (MARBLE.md). One player game: race timer, six races, time
// bonus, game over (marble_flow); stick as trackball, 30 Hz logic tick, camera, enemies;
// HUD (marble_hud).
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
  void loadRace();
  void logicTick(uint32_t keys);
  void publish(void);
  void buildObjs(marble_physics::Mem &m);

  Seqlock<marble_video::VideoState> vblank;
  marble_video::VideoState view = {};   // emulation core
  marble_video::VideoState video = {};  // render copy, video core only
  // level buffers: written and read on the video core only (prepare_frame, render_row)
  marble_level::Store store;
  const marble_level::Level *level = nullptr;
  uint8_t loaded = 0xff;
  uint8_t patched = marble_level::NO_PATCH;  // playfield patch applied to the loaded map
  marble_hud::Grid hud = {};  // video core: alpha cells of video.hud
  uint32_t prev_keys = 0;
  // emulation core: race state lives in the work RAM image, machineBase::memory
  uint8_t race = 0;
  marble_flow::Flow flow = {};
  uint32_t patchRec = 0;   // lastPatch record that view.patch was mapped from
  bool odd = false;        // frame without a logic tick
};

#endif // ENABLE_MARBLE
#endif // MARBLE_H
