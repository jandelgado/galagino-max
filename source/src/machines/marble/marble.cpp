#include "marble.h"

#ifdef ENABLE_MARBLE

#include "marble_camera.h"
#include "marble_drawlist.h"
#include "marble_enemies.h"
#include "marble_flow.h"
#include "marble_objects.h"
#include "marble_occlusion.h"
#include "marble_physics.h"
#include "marble_rng.h"

using namespace marble_video;
using marble_physics::Mem;
using marble_physics::FRAMES;
using marble_physics::PLAYER;

// Stick as trackball (MARBLE.md "Logic tick"): counter delta per tick, k = 9 is about MAME's
// keyboard trackball speed. Screen right: c7 = c6 = -k; down: c7 = +k, c6 = -k.
#define MARBLE_STICK_K 9

static const uint16_t RNG_TICK = 0x100;   // main loop rng(0x100) 0x118a4
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
  marble_flow::newGame(flow);
  loadRace();
}

void marble::start() {
  store.init();  // guarded, start() runs twice
}

// race flow.race on a cleared work RAM image with what a race keeps (timer, score, RNG),
// camera at the race top, view centred; the flow's race intro follows
void marble::loadRace() {
  const marble_flow::Carry c = marble_flow::carry(flow, Mem(memory, race));
  race = flow.race;
  odd = true;  // the first logic tick runs in the first race frame
  memset(memory, 0, marble_physics::RAM_SIZE);
  patchRec = 0;
  view.patch = marble_level::NO_PATCH;
  Mem m(memory, race);
  marble_flow::restore(m, c);
  marble_physics::startRace(m, race);
  marble_flow::raceLoaded(flow, m);
  marble_camera::start(m, race);
  marble_physics::project(m);
  marble_occlusion::enter(m, marble_physics::PLAYER);
  buildObjs(m);
  view.sx = PAN / 2;
  publish();
}

// one 30 Hz logic tick: queued animation frame 0x1344c, camera 0x13ee6 with the band spawns
// 0x144e4 before the player update (0x10fce), steelies 0x158cc, the slot update 0x12fd0,
// rng 0x118a4, then the MO list 0x26f3e
void marble::logicTick(uint32_t keys) {
  int8_t c6 = 0, c7 = 0;
  if (!(keys & BUTTON_EXTRA)) { stick(keys, c6, c7); }  // EXTRA + UP/DOWN is volume

  Mem m(memory, race);
  marble_objects::applyQueued(m);

  const int16_t oldCam = m.sw(marble_physics::CAMERA);
  const uint32_t oldL = m.rl(marble_physics::CAMERA_L);
  marble_camera::tick(m);
  const int16_t newCam = m.sw(marble_physics::CAMERA);
  if (newCam != oldCam) {
    // 0x13ee6 spawns before it stores the new camera: a new steelie projects with the old
    const uint32_t newL = m.rl(marble_physics::CAMERA_L);
    m.wl(marble_physics::CAMERA_L, oldL);
    marble_objects::cameraBands(m, oldCam, newCam);
    m.wl(marble_physics::CAMERA_L, newL);
  }

  marble_physics::tick(m, c6, c7);
  marble_enemies::update(m);
  marble_objects::update(m);
  marble_rng::rng(m, RNG_TICK);  // result unused
  marble_drawlist::sort(m);  // 0x26f3e head
  buildObjs(m);
}

// object sprites, the marble and the masks of the tick, with the camera the slot update saw
void marble::buildObjs(Mem &m) {
  marble_drawlist::build(m, m.sw(marble_physics::CAMERA), MARBLE_CULL_MARGIN, view);
}

// view and marble sprite position from the work RAM image
void marble::publish(void) {
  Mem m(memory, race);
  // projection of the last tick (+0x1e/+0x20), as the arcade MO list and the masks use it
  const int16_t sx = m.sw(PLAYER + 0x1e), sy = m.sw(PLAYER + 0x20);
  const int16_t top = marble_camera::top(m);

  // arcade screen centred in the taller view; race height = range + ARCADE_H
  const int16_t maxy = marble_camera::range(m) + ARCADE_H - OUT_H;
  view.sx = marble_camera::pan(view.sx, sx);
  view.sy = top - MARGIN;
  if (view.sy > maxy) { view.sy = maxy; }
  if (view.sy < 0) { view.sy = 0; }
  view.level = race;
  view.mx = sx + MARBLE_X;
  view.my = top + ARCADE_H + MARBLE_Y - sy;
  const uint32_t rec = marble_objects::lastPatch(m);
  if (rec != patchRec) {  // map record -> table index, only on change
    patchRec = rec;
    view.patch = marble_level::patchIndex(rec);
  }
  vblank.publish([this](VideoState &s) { s = view; });
}

void marble::run_frame(void) {
  const uint32_t keys = input->buttons_get();
  const uint32_t pressed = keys & ~prev_keys;
  prev_keys = keys;

  // START (EXTRA) or FIRE starts a new game after game over
  const bool start = pressed & (BUTTON_EXTRA | BUTTON_FIRE);
  Mem m(memory, race);
  const marble_flow::Step step = marble_flow::frame(flow, m, start);
  if (step == marble_flow::START_RACE) {
    loadRace();
    return;
  }

  // race on: logic on every second frame, as the arcade main loop waits for two vblanks;
  // 0x400010 counts logic ticks (the vblank IRQ adds 1 once the loop set 0x40039a)
  if (step == marble_flow::LOGIC) {
    odd = !odd;
    if (!odd) {
      m.wl(FRAMES, m.rl(FRAMES) + 1);
      logicTick(keys);
    }
  }
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
