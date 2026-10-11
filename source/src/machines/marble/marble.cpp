#include "marble.h"

#ifdef ENABLE_MARBLE

#include "marble_camera.h"
#include "marble_drawlist.h"
#include "marble_objects.h"
#include "marble_occlusion.h"
#include "marble_physics.h"

using namespace marble_video;
using marble_physics::Mem;
using marble_physics::FRAMES;
using marble_physics::PLAYER;

// Stick as trackball (MARBLE.md "Logic tick"): counter delta per tick, k = 9 is about MAME's
// keyboard trackball speed. Screen right: c7 = c6 = -k; down: c7 = +k, c6 = -k.
#define MARBLE_STICK_K 9

static const uint8_t DEATH_TICKS = 30;    // 60 frames after state 2/4 (placeholder)
static const int16_t BODY_X = 8;          // MO 0x05 left of the marble centre sx
static const int16_t BODY_Y = 224;        // MO 0x05 top = screen line 224 - sy
// Remake view is 24 lines taller above and below the arcade screen: object culls move out
static const int16_t MARBLE_CULL_MARGIN = 24;

// stick -> trackball deltas c6/c7; diagonals scaled by 1/sqrt(2) to keep the length
static void stick(uint32_t keys, int8_t &c6, int8_t &c7) {
  const int16_t k = MARBLE_STICK_K;
  int16_t a = 0, b = 0, n = 0;  // c7, c6
  if (keys & BUTTON_RIGHT) { a -= k; b -= k; n++; }
  if (keys & BUTTON_LEFT)  { a += k; b += k; n++; }
  if (keys & BUTTON_DOWN)  { a += k; b -= k; n++; }
  if (keys & BUTTON_UP)    { a -= k; b += k; n++; }
  if (n == 2) {
    a = a * 181 / 256;
    b = b * 181 / 256;
  }
  c7 = (int8_t)a;
  c6 = (int8_t)b;
}

void marble::reset() {
  machineBase::reset();
  prev_keys = 0;
  patchRec = 0;
  view.patch = video.patch = marble_level::NO_PATCH;
  game_started = 1;  // no boot phase
  beginRace(0);
}

void marble::start() {
  store.init();  // guarded, start() runs twice
}

// race start on a cleared work RAM image, camera at the race top, view centred
void marble::beginRace(uint8_t n) {
  race = n;
  dead_ticks = 0;
  memset(memory, 0, marble_physics::RAM_SIZE);
  patchRec = 0;
  view.patch = marble_level::NO_PATCH;
  Mem m(memory, race);
  marble_physics::startRace(m, race);
  marble_camera::start(m, race);
  marble_physics::project(m);
  marble_occlusion::enter(m, marble_physics::PLAYER);  // first tick comes 2 frames later
  buildObjs(m);
  view.sx = PAN / 2;
  publish();
}

// one 30 Hz logic tick: queued animation frame 0x1344c, camera 0x13ee6 with the band spawns
// 0x144e4 before the player update (0x10fce), then the slot update 0x12fd0
void marble::logicTick(uint32_t keys) {
  int8_t c6 = 0, c7 = 0;
  if (!(keys & BUTTON_EXTRA)) { stick(keys, c6, c7); }  // EXTRA + UP/DOWN is volume

  Mem m(memory, race);
  marble_objects::applyQueued(m);

  const int16_t oldCam = m.sw(marble_physics::CAMERA);
  marble_camera::tick(m);
  const int16_t newCam = m.sw(marble_physics::CAMERA);
  if (newCam != oldCam) { marble_objects::cameraBands(m, oldCam, newCam); }

  // dead: the marble stays put (projected, so the camera settles), then respawns
  const uint8_t st = marble_physics::state(m);
  if (st != marble_physics::BROKEN && st != marble_physics::LOST) {
    marble_physics::tick(m, c6, c7);
  } else {
    const bool respawn = ++dead_ticks >= DEATH_TICKS;
    if (respawn) {
      dead_ticks = 0;
      marble_physics::respawn(m);
    }
    marble_physics::project(m);
    if (respawn) { marble_occlusion::enter(m, PLAYER); }  // masks and +0x1c of the new position
  }
  marble_objects::update(m);
  marble_drawlist::sort(m);  // 0x26f3e head
  buildObjs(m);
}

// level object sprites of the tick, with the camera the slot update saw
void marble::buildObjs(Mem &m) {
  view.n = marble_drawlist::build(m, m.sw(marble_physics::CAMERA), MARBLE_CULL_MARGIN, view.objs,
                                  OBJS, view.marbleAt, view.lift);
}

// view and marble sprite from the work RAM image
void marble::publish(void) {
  Mem m(memory, race);
  // projection of the last tick (+0x1e/+0x20), as the arcade MO list and the masks use it
  const int16_t sx = m.sw(PLAYER + 0x1e), sy = m.sw(PLAYER + 0x20);
  const int16_t top = marble_camera::top(m);
  const uint8_t st = marble_physics::state(m);

  // arcade screen centred in the taller view; race height = range + ARCADE_H
  const int16_t maxy = marble_camera::range(m) + ARCADE_H - OUT_H;
  view.sx = marble_camera::pan(view.sx, sx);
  view.sy = top - MARGIN;
  if (view.sy > maxy) { view.sy = maxy; }
  if (view.sy < 0) { view.sy = 0; }
  view.level = race;
  // hidden when dead or every column is covered (+0x1c = 0)
  view.marble = (st == marble_physics::BROKEN || st == marble_physics::LOST ||
                 !m.rb(PLAYER + 0x1c)) ? HIDDEN : SHOWN;
  view.mx = sx - BODY_X;
  view.my = top + BODY_Y - sy;
  const uint32_t rec = marble_objects::lastPatch(m);
  if (rec != patchRec) {  // map record -> table index, only on change
    patchRec = rec;
    view.patch = marble_level::patchIndex(rec);
  }
  uint8_t n = 0;
  for (uint8_t i = 0; i < MASKS; i++) {
    const uint32_t e = PLAYER + 0x38 + 6 * i;
    const uint16_t code = m.rw(e);
    if (!code) { break; }
    // guard for Phase 4 writers of +0x38 (respawn sparkle 0x13d38): not a mask; every
    // 0x264aa code has bit 14, so this never fires yet
    if (!(code & 0x4000)) { n = 0; break; }
    const int16_t h = ((code >> 11) & 7) + 1;
    view.masks[n++] = {code, (int16_t)(m.sw(e + 2) - view.mx),
                       (int16_t)(sy - m.sw(e + 4) + 16 - 8 * h)};
  }
  for (; n < MASKS; n++) { view.masks[n] = {}; }
  vblank.publish([this](VideoState &s) { s = view; });
}

void marble::run_frame(void) {
  const uint32_t keys = input->buttons_get();
  const uint32_t pressed = keys & ~prev_keys;
  prev_keys = keys;
  if (pressed & BUTTON_FIRE) {
    beginRace((race + 1) % marble_level::LEVELS);
    return;
  }

  // logic on every second frame, as the arcade main loop waits for two vblanks
  Mem m(memory, race);
  m.wl(FRAMES, m.rl(FRAMES) + 1);
  if (!(m.rl(FRAMES) & 1)) { logicTick(keys); }
  publish();
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
    patched = marble_level::NO_PATCH;
  }
  if (video.patch != patched) {  // video core owns the map
    patched = video.patch;
    store.patch(patched);
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
