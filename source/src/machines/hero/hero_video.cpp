#ifdef ENABLE_HERO

#include "hero_video.h"
#include "hero_cmap.h"
#include "hero_data.h"
#include <string.h>

using namespace hero_data;
using namespace hero_room;

namespace hero_video {

static inline uint8_t idx(uint8_t tia_color) { return tia_color >> 1; }

static void playfield(uint64_t cells, uint8_t color, uint8_t *dst) {
  for (int c = 0; c < 40; c++) {
    if ((cells >> c) & 1) { memset(dst + 4 * c, idx(color), 4); }
  }
}

// $F284: band colours; the dark first/last lines are the bare hue ($D304)
static uint8_t bandColor(const VideoState &v, int band) {
  if (band == BAND_MID && v.room.lava) { return b1(LAVA_COLOR + ((v.frame >> 3) & 1)); }
  return b1(BAND_LUM + (BANDS - 1 - band)) | v.room.hue;
}

void prepare(Frame &f) {
  for (int b = 0; b < BANDS; b++) {
    f.cells[b] = cells(f.v.room.band[b]);
    // the ball is 8 px wide at x = 8, 60 or 152, two whole cells
    if (f.v.room.ball_x != NO_BALL) { f.cells[b] |= 3ull << (f.v.room.ball_x / 4); }
  }
}

static void bandLine(const Frame &f, int band, int y, int height, bool dark_first, uint8_t *dst) {
  const bool dark = (dark_first && y == 0) || (band != BAND_BOTTOM && y == height - 1);
  playfield(f.cells[band], dark ? f.v.room.hue : bandColor(f.v, band), dst);
}

// $D12C: rounded top edge, no ball yet
static void capLine(const VideoState &v, int y, uint8_t *dst) {
  const int x = 4 - y;
  Band b = v.room.band[BAND_TOP];
  const uint8_t mask = b0(CAP_PF + x);
  b.pf0 = b0(CAP_PF0 + x);
  b.pf1 &= mask;
  b.pf2 &= mask;
  playfield(cells(b), b0(CAP_LUM + x) | v.room.hue, dst);
}

// $D30C: floor waves, water shows through on deep and miner screens ($F60C)
static void floorLine(const VideoState &v, int y, uint8_t *dst) {
  if (v.room.water) { memset(dst, idx(b0(WATER_COLOR + (v.level & 3))), SRC_W); }
  const uint8_t anim = v.room.water ? (v.frame >> 2) & 7 : 0;
  const uint8_t wave = b0((0xdc00 | b0(FLOOR_WAVE + anim)) + y);
  Band b = v.room.band[BAND_BOTTOM];
  b.pf0 = wave;
  b.pf1 &= wave;
  b.pf2 &= wave;
  playfield(cells(b), b0(CAP_LUM + 1 + y) | v.room.hue, dst);
}

static void digit(uint8_t d, int x, int row, uint8_t color, uint8_t *dst) {
  const uint8_t bits = b0(DIGITS + d * 8 + 7 - row);
  for (int i = 0; i < 8; i++) {
    if (bits & (0x80 >> i)) { dst[x + i] = idx(color); }
  }
}

// room viewer HUD: level and screen, two digits each
static void hudLine(const VideoState &v, int y, uint8_t *dst) {
  static const int DIGITS_Y = 36;
  static const uint8_t DIGIT_COLOR = 0x1e;
  memset(dst, idx(HUD_COLOR), SRC_W);
  if (y < DIGITS_Y || y >= DIGITS_Y + 8) { return; }
  const int row = y - DIGITS_Y;
  const uint8_t level = v.level + 1;
  digit(level / 10, 56, row, DIGIT_COLOR, dst);
  digit(level % 10, 64, row, DIGIT_COLOR, dst);
  digit(v.screen / 10, 88, row, DIGIT_COLOR, dst);
  digit(v.screen % 10, 96, row, DIGIT_COLOR, dst);
}

void composeLine(const Frame &f, int line, uint8_t *dst) {
  memset(dst, idx(BLACK), SRC_W);
  if (line >= CAP_Y && line < TOP_Y) {
    capLine(f.v, line - CAP_Y, dst);
  } else if (line >= TOP_Y && line < MID_Y) {
    bandLine(f, BAND_TOP, line - TOP_Y, MID_Y - TOP_Y, false, dst);
  } else if (line >= MID_Y && line < BOTTOM_Y) {
    bandLine(f, BAND_MID, line - MID_Y, BOTTOM_Y - MID_Y, true, dst);
  } else if (line >= BOTTOM_Y && line < FLOOR_Y) {
    bandLine(f, BAND_BOTTOM, line - BOTTOM_Y, FLOOR_Y - BOTTOM_Y, true, dst);
  } else if (line >= FLOOR_Y && line < HUD_Y) {
    floorLine(f.v, line - FLOOR_Y, dst);
  } else if (line >= HUD_Y && line < HUD_END) {
    hudLine(f.v, line - HUD_Y, dst);
  }
}

void scaleLine(const uint8_t *src, uint16_t *dst) {
  for (int x = 0; x < OUT_W; x++) { dst[x] = hero_palette[src[x * 2 / 3]]; }
}

} // namespace hero_video

#endif // ENABLE_HERO
