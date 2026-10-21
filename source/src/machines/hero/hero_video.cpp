#include "../../catalog.h"
#ifdef GAME_HERO

#include "hero_video.h"
#include "hero_cmap.h"
#include "hero_data.h"
#include <string.h>

using namespace hero_data;
using namespace hero_room;

namespace hero_video {

static const int HMOVE_BAR = 8;  // pixels 0-7 are black on every kernel line
static const int OBJ_ROWS = 13;  // band object sprites
static const int OBJ_LINE = 30;  // first sprite row 0 at OBJ_LINE + 39 s - offset
static const int OBJ2_LINE = 46; // second sprite row y at OBJ2_LINE + 39 s - y
static const int BAND_LINES = 39;
static const int LASER_ROW = 5;  // hero row with colour bit 1 ($DD88)
static const uint8_t NO_GFX = 0;

static inline uint8_t idx(uint8_t tia_color) { return tia_color >> 1; }

// --------------------------------------------------------------- objects

// band of a kernel line, screen order (0 top), -1 outside the bands
static int bandOf(int line) {
  if (line < TOP_Y || line >= FLOOR_Y) { return -1; }
  return line == TOP_Y ? 0 : (line - TOP_Y - 1) / BAND_LINES;
}

// one 8 px player row: graphics and colour of `line`, 0 = nothing
struct Row {
  uint8_t bits, color;
};

static uint8_t mirror(uint8_t b, bool m) {
  if (!m) { return b; }
  uint8_t r = 0;
  for (int i = 0; i < 8; i++) { r |= ((b >> i) & 1) << (7 - i); }
  return r;
}

// $D23A: first sprite, 13 rows from page $DB, moved up by its offset
static Row objRow(const VideoState &v, int s, int line) {
  const Sprite &o = v.obj[2 - s];
  const uint8_t y = OBJ_LINE + BAND_LINES * s - o.offset - line;
  if (y >= OBJ_ROWS || o.x >= 0x93) { return Row{NO_GFX, 0}; }
  return Row{mirror(b0(0xdb00 + o.gfx + y), v.p0_mirror), b0(0xdd00 + o.color + y)};
}

// $D2E5: second sprite, rows 12..1 from page $DA
static Row obj2Row(const VideoState &v, int s, int line) {
  const Sprite &o = v.obj2[2 - s];
  const int y = OBJ2_LINE + BAND_LINES * s - line;
  if (y < 1 || y >= OBJ_ROWS || o.x >= 0x93) { return Row{NO_GFX, 0}; }
  return Row{mirror(b0(0xda00 + o.gfx + y), v.p0_mirror), b0(0xdd00 + o.color + y)};
}

// $D17A: P1 row k, enabled by colour bit 0; bit 1 enables M1
static Row heroRow(const VideoState &v, int line) {
  if (v.hero_page == NO_HERO) { return Row{NO_GFX, 0}; }
  const int k = line - (HERO_TOP_LINE - v.hero_y);
  if (k < 0 || k >= HERO_ROWS) { return Row{NO_GFX, 0}; }
  const uint8_t color = b0(0xdd8d - k);
  if (!(color & 1)) { return Row{NO_GFX, color}; }
  const uint8_t bits = b0((v.hero_page << 8 | v.hero_frame) + 0x19 - k);
  return Row{mirror(bits, v.hero_mirror), color};
}

// the kernel writes ENAM1 on band lines 2-38 only, it stays off on the others
static bool laserOn(const VideoState &v, int line) {
  const int s = bandOf(line);
  const int in_band = line - TOP_Y - 1 - BAND_LINES * s;
  if (s < 0 || in_band < 1 || in_band > BAND_LINES - 2) { return false; }
  return line - (HERO_TOP_LINE - v.hero_y) == LASER_ROW;
}

// 160 px masks, bit p = pixel p
struct Mask {
  uint32_t w[5];
  void clear() { memset(w, 0, sizeof(w)); }
  void set(int x, int n) {
    for (int p = x; p < x + n; p++) {
      if (p >= 0 && p < SRC_W) { w[p >> 5] |= 1u << (p & 31); }
    }
  }
  void bits(int x, uint8_t b) {
    for (int i = 0; i < 8; i++) {
      if (b & (0x80 >> i)) { set(x + i, 1); }
    }
  }
  bool hits(const Mask &o) const {
    for (int i = 0; i < 5; i++) {
      if (w[i] & o.w[i]) { return true; }
    }
    return false;
  }
};

static void playfieldMask(uint64_t cells, Mask &m) {
  for (int c = 0; c < 40; c++) {
    if ((cells >> c) & 1) { m.set(4 * c, 4); }
  }
}

void collide(const VideoState &v, Latches &l) {
  memset(&l, 0, sizeof(l));
  for (int line = TOP_Y; line < FLOOR_Y; line++) { // from CXCLR after the cap ($D163)
    const int s = bandOf(line);
    const int i = 2 - s; // original band index
    const int in_band = line - TOP_Y - 1 - BAND_LINES * s;

    Mask p1, m1, p0, pf, bl;
    p1.clear();
    m1.clear();
    p0.clear();
    pf.clear();
    bl.clear();
    p1.bits(v.hero_x, heroRow(v, line).bits);
    if (laserOn(v, line)) { m1.set(v.laser_x - 1, v.ball_w); }
    const Row o = objRow(v, s, line);
    const Row o2 = obj2Row(v, s, line);
    p0.bits(v.obj[i].x, o.bits);
    p0.bits(v.obj2[i].x, o2.bits);
    playfieldMask(cells(v.band[s]), pf);
    bl.set((v.ball ? v.ball : 1) - 1, v.ball_w); // $D00C: 0 positions at 1

    // CXPPMM read after the first sprite, CXM1P before the second ($D263, $D2B0)
    if (in_band < 24 && p0.hits(p1)) { l.hero_obj[i] = 0x80; }
    if (in_band < 25 && p0.hits(m1)) { l.laser_obj[i] = 0x80; }
    if (i == 0 && p0.hits(p1)) { l.bottom_pp = 0x80; }
    if (m1.hits(pf)) { l.laser_wall |= 0x80; }
    if (m1.hits(bl)) { l.laser_wall |= 0x40; }
  }
}

// ------------------------------------------------------------- drawing

static void playfield(uint64_t cells, uint8_t color, uint8_t *dst) {
  for (int c = 0; c < 40; c++) {
    if ((cells >> c) & 1) { memset(dst + 4 * c, idx(color), 4); }
  }
}

static void sprite(int x, Row r, uint8_t *dst) {
  for (int i = 0; i < 8; i++) {
    if ((r.bits & (0x80 >> i)) && x + i < SRC_W) { dst[x + i] = idx(r.color); }
  }
}

void prepare(Frame &f) {
  for (int b = 0; b < BANDS; b++) {
    f.cells[b] = cells(f.v.band[b]);
    // the ball is 4 px aligned ($A0 - 1, $A0 = $FE | 1)
    if (f.v.ball) { f.cells[b] |= (f.v.ball_w == 8 ? 3ull : 1ull) << ((f.v.ball - 1) / 4); }
  }
}

// $D17A: background, hero and laser, band objects above, playfield on top
static void bandLine(const Frame &f, int line, uint8_t *dst) {
  const VideoState &v = f.v;
  const int s = bandOf(line);
  memset(dst, idx(v.background), SRC_W);

  const Row h = heroRow(v, line);
  sprite(v.hero_x, h, dst);
  if (laserOn(v, line)) {
    for (int p = v.laser_x - 1; p < v.laser_x - 1 + v.ball_w && p < SRC_W; p++) { dst[p] = idx(h.color); }
  }
  if (line > TOP_Y) {
    const int i = 2 - s;
    sprite(v.obj2[i].x, obj2Row(v, s, line), dst);
    sprite(v.obj[i].x, objRow(v, s, line), dst);
  }

  // dark band edges ($D304): the first line of the middle and bottom band,
  // the last of the top and middle band
  const bool dark = line == MID_Y - 1 || line == MID_Y || line == BOTTOM_Y - 1 || line == BOTTOM_Y;
  const int band = line < MID_Y ? BAND_TOP : (line < BOTTOM_Y ? BAND_MID : BAND_BOTTOM);
  playfield(f.cells[band], dark ? v.hue : v.band_color[band], dst);
}

// $D12C: rounded top edge; ENABL turns the ball on during its last line,
// about pixel 44 ($D14F)
static void capLine(const VideoState &v, int y, uint8_t *dst) {
  static const int BALL_ON_X = 44;
  const int x = 4 - y;
  memset(dst, idx(v.background), SRC_W);
  Band b = v.band[BAND_TOP];
  const uint8_t mask = b0(CAP_PF + x);
  b.pf0 = b0(CAP_PF0 + x);
  b.pf1 &= mask;
  b.pf2 &= mask;
  uint64_t c = cells(b);
  const int ball = v.ball - 1;
  if (x == 0 && v.ball && ball >= BALL_ON_X) { c |= (v.ball_w == 8 ? 3ull : 1ull) << (ball / 4); }
  playfield(c, b0(CAP_LUM + x) | v.hue, dst);
}

// $D30C: floor waves, water shows through on deep and miner screens
static void floorLine(const VideoState &v, int y, uint8_t *dst) {
  memset(dst, idx(v.water ? v.water_color : v.background), SRC_W);
  const uint8_t wave = b0((0xdc00 | b0(FLOOR_WAVE + v.wave)) + y);
  Band b = v.band[BAND_BOTTOM];
  b.pf0 = wave;
  b.pf1 &= wave;
  b.pf2 &= wave;
  playfield(cells(b), b0(CAP_LUM + 1 + y) | v.hue, dst);
}

// ------------------------------------------------------------------ HUD

static const int POWER_Y = 3;      // HUD lines: "POWER" and the bar
static const int LIVES_Y = 10;     // 13 rows
static const int DYNAMITE_Y = 24;  // 11 rows
static const int TEXT_Y = 37;      // 8 rows
static const int POWER_TEXT_X = 24;
static const int POWER_BAR_X = 49;
static const int POWER_BAR_END = 128;
static const int ICONS_X = 56;     // 6 icon slots, 8 px apart, filled from the right
static const int TEXT_X = 56;
static const uint8_t POWER_COLOR = 0x1c;
static const uint8_t POWER_USED = 0x42;
static const uint8_t LABEL_COLOR = 0x0c;

static void icons(int n, uint16_t gfx, uint16_t color, int row, uint8_t *dst) {
  const Row r = Row{b0(gfx + row), b0(color + row)};
  for (int i = 6 - n; i < 6; i++) { sprite(ICONS_X + 8 * i, r, dst); }
}

// $D3A0
static void hudLine(const VideoState &v, int y, uint8_t *dst) {
  memset(dst, idx(HUD_COLOR), SRC_W);
  if (y >= POWER_Y && y < POWER_Y + 5) {
    const int x = 4 - (y - POWER_Y);
    const uint8_t text[3] = {b0(0xde72 + x), b0(0xde77 + x), b0(0xde7c + x)};
    for (int i = 0; i < 3; i++) { sprite(POWER_TEXT_X + 8 * i, Row{text[i], LABEL_COLOR}, dst); }
    // full power reaches into the grey: the bar ends at 127
    const int end = POWER_BAR_X + v.power;
    const int used_end = end < POWER_BAR_END - 1 ? POWER_BAR_END : POWER_BAR_END - 1;
    for (int p = POWER_BAR_X; p < used_end; p++) { dst[p] = idx(p < end ? POWER_COLOR : POWER_USED); }
  } else if (y >= LIVES_Y && y < LIVES_Y + 13) {
    if (v.lives > 1) { icons(v.lives - 1 > 6 ? 6 : v.lives - 1, 0xdda9, 0xddb7, 12 - (y - LIVES_Y), dst); }
  } else if (y >= DYNAMITE_Y && y < DYNAMITE_Y + 11) {
    icons(v.dynamite > 6 ? 6 : v.dynamite, 0xda34, 0xdd69, 10 - (y - DYNAMITE_Y), dst);
  } else if (y >= TEXT_Y && y < TEXT_Y + 8) {
    const int row = 7 - (y - TEXT_Y);
    for (int i = 0; i < 6; i++) { sprite(TEXT_X + 8 * i, Row{b0(0xdf00 + v.text[i] + row), v.text_color}, dst); }
  }
}

// $D4BD: the copyright text's bottom rows, then the logo's top rows; the
// attract mode flips between the two
static void logoLine(const VideoState &v, int line, uint8_t *dst) {
  static const int LOGO_X = 64;  // 5 graphics bytes
  static const int RAINBOW_X = 56;
  static const int RAINBOW_W = 12; // PF2 $1C
  const int k = v.copyright - (line - LOGO_Y);
  if (k >= 0) { // copyright rows k..0
    for (int i = 0; i < 6; i++) { sprite(TEXT_X + 8 * i, Row{b0(0xdfa8 + 8 * i + k), LABEL_COLOR}, dst); }
    return;
  }
  const int r = 7 - (line - LOGO_Y - v.copyright - 1); // logo rows 7..copyright
  if (r < 0) { return; }
  const uint8_t rainbow = idx(b0(0xddec + r));
  memset(dst + RAINBOW_X, rainbow, RAINBOW_W);
  // the ball, moved 1 px left per line, tapers the rainbow
  const int ball = RAINBOW_X + RAINBOW_W - (7 - r);
  memset(dst + ball - 1, rainbow, 8);
  for (int i = 0; i < 5; i++) { sprite(LOGO_X + 8 * i, Row{b0(0xdf80 + 8 * i + r), LABEL_COLOR}, dst); }
}

void composeLine(const Frame &f, int line, uint8_t *dst) {
  memset(dst, idx(BLACK), SRC_W);
  if (f.v.blank || line < 0 || line >= SRC_H) { return; }
  if (line >= CAP_Y && line < TOP_Y) {
    capLine(f.v, line - CAP_Y, dst);
  } else if (line >= TOP_Y && line < FLOOR_Y) {
    bandLine(f, line, dst);
  } else if (line >= FLOOR_Y && line < HUD_Y) {
    floorLine(f.v, line - FLOOR_Y, dst);
  } else if (line >= HUD_Y && line < HUD_END) {
    hudLine(f.v, line - HUD_Y, dst);
  } else if (line >= LOGO_Y) {
    logoLine(f.v, line, dst);
    return;
  } else {
    return;
  }
  memset(dst, idx(BLACK), HMOVE_BAR);
}

void scaleLine(const uint8_t *src, uint16_t *dst) {
  for (int x = 0; x < OUT_W; x++) { dst[x] = hero_palette[src[x * 2 / 3]]; }
}

} // namespace hero_video

#endif // GAME_HERO
