#ifndef MARBLE_H
#define MARBLE_H

#include "../machineBase.h"
#include "marble_arena_plan.h"

#include "../../catalog.h"
#ifdef GAME_MARBLE

#include "marble_logo.h"
#include "marble_level.h"
#include "marble_video.h"
#include "../../emulation/seqlock.h"
#include "marble_mem.h"
#include "marble_flow.h"
#include "marble_hud.h"
#include "marble_sound.h"
#include "../../emulation/sidplayer.h"

// Marble Madness (Atari 1984). Not emulated: graphics, levels and music come from the
// ROM, the game is reimplemented (MARBLE.md). One player game: race timer, six races, time
// bonus, game over (marble_flow); stick as trackball, 30 Hz logic tick, camera, enemies;
// HUD (marble_hud).
class marble : public machineBase
{
public:
  // Arena::alloc besides planned assets (only marble_tables), read by romconv
  // (arena_plan.py): ctor: level map, tiles, sprite tiles, program ROM,
  // terrain; start(): SID file, SID RAM. Checked in marble.cpp.
  static constexpr uint32_t ARENA_EXTRA[] = { 10440, 57344, 39904, 20291, 6902, 14332, 15360 };

  marble();
  signed char machineType() override { return MCH_MARBLE; }
  const int renderWidth()  override { return marble_video::OUT_W; }
  const int renderBuffer() override { return marble_video::OUT_W * 2 * 8; }

  void reset() override;
  void start() override;
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  void renderAudio(int16_t *out, int n) override;
  static const Logo &logo(void) { return marble_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
#endif

private:
  void loadRace();
  void logicTick(uint32_t keys);
  void publish(void);
  void titleView();
  void publishTail(marble_physics::Mem &m);
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
  marble_hud::Tracker tracker = {};
  // high score table: written on the emulation core only outside the title (6c), read by
  // the video core's title grid
  marble_hiscore::Table table = {};
  uint32_t patchRec = 0;   // lastPatch record that view.patch was mapped from
  bool odd = false;        // frame without a logic tick

  // sound: commands on the emulation core, the C64 tune on the audio core
  marble_sound::Sound sound = {};
  SidPlayer player;
  PsidInfo tune = {};
  bool tune_loaded = false, tune_ok = false;  // start(): unpacked once
  uint16_t music_seen = 0, effect_seen = 0;   // audio core
  bool sid_on = false;                        // audio core
};

#endif // GAME_MARBLE
#endif // MARBLE_H
