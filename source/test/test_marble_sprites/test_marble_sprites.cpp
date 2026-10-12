#include <stdio.h>
#include <string.h>
#include <unity.h>

// Level object sprite tables (marble_rom.h, ./convert.sh marble) against the published object
// lists of the MAME segments (marble_sprites_trace.h): every frame id, tile and set a recorded
// tick publishes exists in the race's sprite set, and the tile sets decode. The draw list
// (insert, remove, sort, build) replays each segment from its initial work RAM and the inputs
// and publishes the recorded list in every tick. Without the generated header the tests skip.
#if __has_include("../../src/machines/marble/marble_rom.h")
#define HAVE_ROM 1
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/machines/marble/marble_rom.h"
#define ENABLE_MARBLE
#include "../../src/machines/marble/marble_mem.cpp"
#include "../../src/machines/marble/marble_rng.cpp"
#include "../../src/machines/marble/marble_physics.cpp"
#include "../../src/machines/marble/marble_enemies.cpp"
#include "../../src/machines/marble/marble_slinkies.cpp"
#include "../../src/machines/marble/marble_acid.cpp"
#include "../../src/machines/marble/marble_birds.cpp"
#include "../../src/machines/marble/marble_minis.cpp"
#include "../../src/machines/marble/marble_pistons.cpp"
#include "../../src/machines/marble/marble_objects.cpp"
#include "../../src/machines/marble/marble_occlusion.cpp"
#include "../../src/machines/marble/marble_drawlist.cpp"
#endif
#include "marble_sprites_trace.h"
#include "../test_marble_level/marble_fixture.h"  // MARBLE_FIX_SWEEP_MAX
#include "../test_marble_deaths/marble_deaths_trace.h"

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static const uint8_t SET_SHADE = 8;
static const uint8_t NO_VARIANT = 0xff;
static const uint8_t MAX_OBJS = 8;
static const uint8_t PENS = 8;   // MO pens 0-7

static const MarbleSpriteSet &setOf(uint8_t race) {
  return marble_sprites[marble_sprite_race[race]];
}

static void test_tiles_decode(void) {
  static uint8_t buf[32 * MARBLE_SPRITE_MAX_TILES];
  for (uint8_t k = 0; k < sizeof(marble_sprite_tiles) / sizeof(marble_sprite_tiles[0]); k++) {
    const MarbleSpriteSet &s = marble_sprites[k];
    TEST_ASSERT_TRUE(s.ntiles <= MARBLE_SPRITE_MAX_TILES);
    Asset<unsigned char[32], COMPRESSED> tiles(marble_sprite_tiles[k]);
    TEST_ASSERT_EQUAL_UINT32(s.ntiles - MARBLE_DEATH_TILES, tiles.size());  // after the death set
    tiles.decodeInto((unsigned char(*)[32])buf);
    for (uint32_t i = 0; i < 32u * tiles.size(); i++) {
      TEST_ASSERT_TRUE((buf[i] >> 4) < PENS && (buf[i] & 15) < PENS);
    }
  }
}

static void test_frames_in_range(void) {
  for (uint8_t k = 0; k < sizeof(marble_sprite_tiles) / sizeof(marble_sprite_tiles[0]); k++) {
    const MarbleSpriteSet &s = marble_sprites[k];
    for (uint8_t i = 1; i < s.nhdr; i++) { TEST_ASSERT_TRUE(s.addr[i - 1] < s.addr[i]); }
    for (uint8_t f = 0; f < s.nframes; f++) {
      const MarbleSpriteFrame &fr = s.frames[f];
      for (uint8_t c = 0; c < fr.ncols; c++) {
        const MarbleSpriteCol &col = s.cols[fr.first + c];
        TEST_ASSERT_TRUE((col.h & 0x7f) >= 1);
        TEST_ASSERT_TRUE(col.tile >= MARBLE_DEATH_TILES);
        TEST_ASSERT_TRUE(col.tile + (col.h & 0x7f) <= s.ntiles);
      }
    }
  }
}

// Death set: tiles decode (pens 0-7), stars and columns inside it, header ids ascending, and
// every frame header the death fixture draws (rl(+0x5a), overlay rl(+0x62)) has an id.
static void test_death_set(void) {
  static const uint32_t FRAMES_BASE = 0x20000;
  static uint8_t buf[32 * MARBLE_DEATH_TILES];
  const MarbleSpriteSet &s = marble_physics::deathSet();
  TEST_ASSERT_EQUAL_UINT16(MARBLE_DEATH_TILES, s.ntiles);
  TEST_ASSERT_EQUAL_UINT8(s.nhdr, s.nframes);
  TEST_ASSERT_TRUE(MARBLE_STAR_TILE + MARBLE_STARS <= MARBLE_DEATH_TILES);
  Asset<unsigned char[32], COMPRESSED> tiles(marble_death_tiles_blob);
  TEST_ASSERT_EQUAL_UINT32(MARBLE_DEATH_TILES, tiles.size());
  tiles.decodeInto((unsigned char(*)[32])buf);
  for (uint32_t i = 0; i < sizeof(buf); i++) {
    TEST_ASSERT_TRUE((buf[i] >> 4) < PENS && (buf[i] & 15) < PENS);
  }

  for (uint8_t i = 1; i < s.nhdr; i++) { TEST_ASSERT_TRUE(s.addr[i - 1] < s.addr[i]); }
  for (uint8_t f = 0; f < s.nframes; f++) {
    const MarbleSpriteFrame &fr = s.frames[f];
    TEST_ASSERT_TRUE(fr.ncols > 0);
    for (uint8_t c = 0; c < fr.ncols; c++) {
      const MarbleSpriteCol &col = s.cols[fr.first + c];
      TEST_ASSERT_TRUE((col.h & 0x7f) >= 1);
      TEST_ASSERT_TRUE(col.tile + (col.h & 0x7f) <= s.ntiles);
    }
  }

  uint16_t drawn = 0;
  for (uint8_t k = 0; k < MARBLE_DEATHS_SEGMENTS; k++) {
    const MarbleDeathSegment &seg = marble_deaths_segments[k];
    for (uint16_t t = 0; t < seg.nticks; t++) {
      const uint32_t hdrs[2] = {seg.ticks[t].frame_hdr, seg.ticks[t].shard_hdr};
      for (uint32_t h : hdrs) {
        if (!h) { continue; }
        TEST_ASSERT_EQUAL_UINT16((h - FRAMES_BASE) >> 1, s.addr[marble_physics::deathFrame(h)]);
        drawn++;
      }
    }
  }
  TEST_ASSERT_TRUE(drawn > 0);
}

static void test_published_objects(void) {
  for (uint8_t s = 0; s < MARBLE_SPRITES_SEGMENTS; s++) {
    const MarbleSprSegment &seg = marble_sprites_segments[s];
    const MarbleSpriteSet &set = setOf(seg.race);
    for (uint16_t t = 0; t < seg.nticks; t++) {
      const MarbleSprTick &tk = seg.ticks[t];
      TEST_ASSERT_TRUE(tk.n <= MAX_OBJS && tk.marble_at <= tk.n);
      TEST_ASSERT_TRUE(tk.variant <= 3 || tk.variant == NO_VARIANT);
      TEST_ASSERT_TRUE(tk.first + tk.n <= seg.nobjs);
      for (uint8_t i = 0; i < tk.n; i++) {
        const MarbleSprObj &o = seg.objs[tk.first + i];
        TEST_ASSERT_TRUE(o.frame < set.nframes);
        TEST_ASSERT_TRUE(set.frames[o.frame].ncols > 0);
        TEST_ASSERT_TRUE((o.set == SET_SHADE) == (o.frame >= set.nhdr));
        TEST_ASSERT_TRUE(o.set == SET_SHADE || o.set == 3 || o.set == 5 || o.set == 6 ||
                         o.set == 7);
      }
    }
  }
}

using marble_physics::Mem;
using marble_video::Obj;
using marble_video::VideoState;

static uint8_t ram[marble_physics::RAM_SIZE];

static void loadRam(const MarbleSprSegment &seg) {
  memset(ram, 0, sizeof(ram));
  for (uint8_t i = 0; i < seg.nram; i++) {
    const MarbleSprRam &r = seg.ram[i];
    memcpy(ram + (r.addr - marble_physics::RAM_BASE), r.bytes, r.len);
  }
}

// Player update of a fixture tick. A fixture snapshot is the 0x121b8 entry of a state 0/1
// handler: tick 0 (prev null) finishes that handler (internals of the included
// marble_physics.cpp), tick k runs tick() with the deltas stored at tick k - 1.
static void playerTick(Mem &m, const MarbleSprTick *prev) {
  if (prev) {
    marble_physics::tick(m, prev->c6, prev->c7);
    return;
  }
  const uint8_t st = marble_physics::state(m);
  marble_physics::physics_121b8(m, marble_physics::PLAYER);
  if (st == marble_physics::DIZZY) { marble_physics::countdown_2574c(m, marble_physics::PLAYER); }
}

// One tick as the fixture model: player update, slot update, queued frame, the camera long (an
// input) with the band spawns; then box refresh and sort, and the objects built with the
// camera word the slot update saw.
static void replayTick(Mem &m, const MarbleSprTick *prev, const MarbleSprTick &t,
                       VideoState &v) {
  playerTick(m, prev);
  marble_objects::update(m);
  marble_objects::applyQueued(m);

  const int16_t seen = m.sw(marble_physics::CAMERA);
  m.wl(marble_physics::CAMERA_L, t.camera);
  if ((uint16_t)t.camera != (uint16_t)seen) {
    marble_objects::cameraBands(m, seen, (int16_t)t.camera);
  }

  marble_drawlist::sort(m);
  v = {};
  marble_drawlist::build(m, seen, 0, v);
}

// replayTick with a cull margin (the remake draws 24 lines more above and below)
static uint8_t replayCount(Mem &m, const MarbleSprTick *prev, const MarbleSprTick &t,
                           int16_t margin, VideoState &v) {
  playerTick(m, prev);
  marble_objects::update(m);
  marble_objects::applyQueued(m);
  const int16_t seen = m.sw(marble_physics::CAMERA);
  m.wl(marble_physics::CAMERA_L, t.camera);
  if ((uint16_t)t.camera != (uint16_t)seen) {
    marble_objects::cameraBands(m, seen, (int16_t)t.camera);
  }
  marble_drawlist::sort(m);
  v = {};
  return marble_drawlist::build(m, seen, margin, v);  // count also past OBJS
}

static uint8_t replayCount(Mem &m, const MarbleSprTick *prev, const MarbleSprTick &t,
                           int16_t margin) {
  VideoState v;
  return replayCount(m, prev, t, margin, v);
}

// published level objects of v: without the death set objects (slinkies, acid; the band
// spawns place them, the model's sweep has no enemies)
static uint8_t slotObjs(const VideoState &v) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < v.n; i++) { n += !(v.objs[i].set & marble_video::SET_DEATH); }
  return n;
}

static void test_margin_fits_objs(void) {
  for (uint8_t s = 0; s < MARBLE_SPRITES_SEGMENTS; s++) {
    const MarbleSprSegment &seg = marble_sprites_segments[s];
    loadRam(seg);
    Mem m(ram, seg.race);
    uint8_t most = 0;
    for (uint16_t k = 0; k < seg.nticks; k++) {
      const uint8_t n = replayCount(m, k ? &seg.ticks[k - 1] : nullptr, seg.ticks[k], 24);
      if (n > most) { most = n; }
      char msg[80];
      snprintf(msg, sizeof(msg), "segment %d (race %d) tick %d: %d objects at margin 24", s,
               seg.race, k, n);
      TEST_ASSERT_TRUE_MESSAGE(n <= MAX_OBJS, msg);
    }
    printf("segment %d race %d: most objects at margin 24: %d\n", s, seg.race, most);
  }
}

// Race start as the machine runs it (startRace incl. the draw list), then the camera sweeps the
// race (marble_mo.race_sweep: step 4, down, up, down, silly bottom first) with the slot update,
// sort and build at the remake margin. No abort (every ROM read is in the emitted blocks, the
// sort reads the frame boxes of every list), and the largest level object count per race is
// the model's (MARBLE_FIX_SWEEP_MAX, printed by the converter).
static void test_race_sweep(void) {
  static const uint16_t STEP = 4;
  for (uint8_t race = 0; race < 6; race++) {
    memset(ram, 0, sizeof(ram));
    Mem m(ram, race);
    marble_physics::startRace(m, race);
    const uint16_t top = m.romw(m.desc() + marble_physics::DESC_TOP);
    const uint16_t n = m.romw(m.desc() + marble_physics::DESC_RANGE) / STEP + 1;

    uint8_t most = 0;
    for (uint8_t pass = 0; pass < 3; pass++) {
      for (uint16_t i = 0; i < n; i++) {
        const bool rev = ((pass & 1) != 0) != (race == 4);  // silly starts at the bottom
        MarbleSprTick t = {};
        t.camera = top + STEP * (rev ? n - 1 - i : i);
        VideoState v;
        TEST_ASSERT_TRUE(replayCount(m, &t, t, 24, v) <= marble_video::OBJS);  // race start: whole ticks
        const uint8_t count = slotObjs(v);
        if (count > most) { most = count; }
      }
    }
    char msg[60];
    snprintf(msg, sizeof(msg), "race %d: most objects", race);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(MARBLE_FIX_SWEEP_MAX[race], most, msg);
  }
}

static void test_draw_list(void) {
  for (uint8_t s = 0; s < MARBLE_SPRITES_SEGMENTS; s++) {
    const MarbleSprSegment &seg = marble_sprites_segments[s];
    loadRam(seg);
    Mem m(ram, seg.race);

    for (uint16_t k = 0; k < seg.nticks; k++) {
      const MarbleSprTick &t = seg.ticks[k];
      VideoState v;
      replayTick(m, k ? &seg.ticks[k - 1] : nullptr, t, v);

      // level objects only: the fixture leaves out the slinkies (start RAM entries, not
      // updated here); their masks come before the marble's in the pool
      uint8_t n = 0, at = v.marbleAt, pool = 0;
      const Obj *lvl[marble_video::OBJS];
      for (uint8_t i = 0; i < v.n; i++) {
        if (v.objs[i].set == (marble_video::SET_DEATH | marble_video::SET_SLINKY)) {
          at -= i < v.marbleAt;
          pool += v.objs[i].nmask;
          continue;
        }
        lvl[n++] = &v.objs[i];
      }
      const uint8_t lift = v.lift;

      // level objects have no masks; the marble's masks are the other pool entries
      bool same = n == t.n && at == t.marble_at && lift == t.variant && v.mask == pool &&
                  v.nmask <= 5;
      for (uint8_t i = 0; same && i < n; i++) {
        const MarbleSprObj &o = seg.objs[t.first + i];
        const Obj &g = *lvl[i];
        same = g.frame == o.frame && g.set == o.set && g.x == o.x && g.y == o.y && !g.nmask;
      }
      if (same) { continue; }

      char msg[200];
      snprintf(msg, sizeof(msg),
               "segment %d (race %d frame %u) tick %d: expected n %d marbleAt %d lift %d, got n %d"
               " marbleAt %d lift %d",
               s, seg.race, (unsigned)seg.frame, k, t.n, t.marble_at, t.variant, n, at, lift);
      TEST_FAIL_MESSAGE(msg);
    }
  }
}
#endif

int main(int, char **) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_tiles_decode);
  RUN_TEST(test_frames_in_range);
  RUN_TEST(test_death_set);
  RUN_TEST(test_published_objects);
  RUN_TEST(test_draw_list);
  RUN_TEST(test_margin_fits_objs);
  RUN_TEST(test_race_sweep);
#else
  TEST_IGNORE_MESSAGE("marble_rom.h missing, run romconv ./convert.sh marble");
#endif
  return UNITY_END();
}
