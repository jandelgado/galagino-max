#include "marble_hud.h"

#include "../../catalog.h"
#ifdef GAME_MARBLE

#include <stdio.h>
#include <string.h>
#include "marble_rom.h"
#include "marble_particles.h"
#include "marble_physics.h"

// Port of dev/marble/marble_hud.py (items, view, palette).

namespace marble_hud {

namespace {

using marble_video::Hud;

enum Fill : uint16_t { CLEAR = 0, SOLID = OPAQUE };

// alpha colours (colour * 4 + pen indexes pal)
static const uint8_t TEXT = 0, TIMER = 2, BANNER = 4, NAME = 5, BORDER = 7;

// item moves from arcade alpha cells into the view (MARBLE.md "HUD", remake layout)
static const int8_t TOP_DX = -3, TIMER_DX = -6, BANNER_DX = -6, BANNER_DY = 3;
static const int8_t LABEL_DX = -7, VALUE_DX = -9, FINAL_DX = -4, SCORE_DX = -10;

// big font: digit d = tiles 0x100 + 4d (TL TR / BL BR), space digit wide, comma one column
static const uint16_t BIG_DIGIT = 0x100, BIG_BLANK = 0x13c, BIG_COMMA = 0x16e;
static const uint16_t UNDERLINE = 0x5f, OVERLINE = 0x5e, RIGHT = 0xdf, TOP_RIGHT = 0xff,
                      BOTTOM_RIGHT = 0x1b;

static const uint8_t BLANK_OUT = 16;  // out of time box width
static const uint8_t SECONDS_MAX = 99, DEATHS_MAX = 20, SHOWN_MAX = 99;

// 0x118d2 race end colours: boxes black, 9/13 grey
static const uint8_t BONUS_BLACK[] = {0, 4, 8, 12, 29};
static const uint8_t BONUS_GREY[] = {9, 13};

struct Put {
  Grid &g;
  int8_t dx, dy;

  void cell(int row, int col, uint16_t w) const {
    const int y = row + dy, x = col + dx;
    if (y < 0 || y >= ROWS || x < 0 || x >= COLS) { return; }
    g.cells[y][x] = w;
    g.used[y] = 1;
  }

  void text(int row, int col, const char *s, int n, uint8_t colour, Fill fill) const {
    for (int k = 0; k < n; k++) {
      const uint16_t code = s[k] == ' ' ? 0 : (uint8_t)s[k];
      cell(row, col + k, code | colour << 10 | fill);
    }
  }

  void text(int row, int col, const char *s, uint8_t colour, Fill fill) const {
    text(row, col, s, (int)strlen(s), colour, fill);
  }

  void repeat(int row, int col, int n, uint16_t w) const {
    for (int k = 0; k < n; k++) { cell(row, col + k, w); }
  }

  void tiles(int row, int col, const uint16_t *codes, int n, uint8_t colour) const {
    for (int k = 0; k < n; k++) { cell(row, col + k, codes[k] | colour << 10); }
  }

  // big number string (digits, commas, spaces): two rows from (row, col)
  void big(int row, int col, const char *s, uint8_t colour, Fill fill) const {
    const uint16_t attr = colour << 10 | fill;
    for (; *s; s++) {
      if (*s == ',') {
        cell(row, col, BIG_BLANK | attr);
        cell(row + 1, col++, BIG_COMMA | attr);
        continue;
      }
      const uint16_t t = *s == ' ' ? 0 : BIG_DIGIT + 4 * (*s - '0');
      for (int k = 0; k < 2; k++, col++) {
        cell(row, col, (t ? t + k : BIG_BLANK) | attr);
        cell(row + 1, col, (t ? t + 2 + k : BIG_BLANK) | attr);
      }
    }
  }
};

// columns of a big number string
int bigWidth(const char *s) {
  int w = 0;
  for (; *s; s++) { w += *s == ',' ? 1 : 2; }
  return w;
}

// "1,020": v with thousands commas, right aligned to width (0: no padding)
const char *commas(char *buf, uint32_t v, int width = 0) {
  char digits[12];
  const int n = snprintf(digits, sizeof(digits), "%u", (unsigned)v);
  char tmp[16];
  int len = 0;
  for (int k = 0; k < n; k++) {
    if (k && (n - k) % 3 == 0) { tmp[len++] = ','; }
    tmp[len++] = digits[k];
  }
  const int pad = width > len ? width - len : 0;
  memset(buf, ' ', pad);
  memcpy(buf + pad, tmp, len);
  buf[pad + len] = 0;
  return buf;
}

uint32_t clampU(int32_t v, int32_t hi) { return v < 0 ? 0 : v > hi ? hi : v; }

void top(Grid &g, const Hud &h) {
  const Put p = {g, TOP_DX, 0};
  char buf[16];
  p.repeat(0, 3, 5, UNDERLINE | BORDER << 10);
  p.text(1, 3, "SCORE", TEXT, SOLID);
  p.text(2, 3, commas(buf, h.score), TEXT, SOLID);

  const Put t = {g, TIMER_DX, 0};
  snprintf(buf, sizeof(buf), "%02u", (unsigned)clampU(h.seconds, SHOWN_MAX));
  t.repeat(0, 19, 4, UNDERLINE | BORDER << 10);
  t.cell(0, 23, TOP_RIGHT | BORDER << 10);
  t.big(1, 19, buf, TIMER, SOLID);
  t.cell(1, 23, RIGHT | BORDER << 10);
  t.cell(2, 23, RIGHT | BORDER << 10);
  t.repeat(3, 19, 4, OVERLINE | BORDER << 10);
  t.cell(3, 23, BOTTOM_RIGHT | BORDER << 10);
}

void intro(Grid &g, const Hud &h) {
  const Put p = {g, BANNER_DX, BANNER_DY};
  const uint8_t race = h.race < 6 ? h.race : 5;
  const char *line = marble_s_races[race];
  const char *name = marble_s_names[race];
  p.text(8, 8, marble_s_blank26, BANNER, SOLID);
  p.text(9, 8, race < 2 ? marble_s_time_to : marble_s_extra, BANNER, SOLID);
  p.text(10, 8, line, BANNER, SOLID);
  const char *at = strstr(line, name);
  if (at) { p.text(10, 8 + (int)(at - line), name, NAME, SOLID); }
  p.text(11, 8, marble_s_blank26, BANNER, SOLID);

  char buf[8];
  snprintf(buf, sizeof(buf), "%2u", (unsigned)h.add);
  p.big(9, 29, buf, NAME, SOLID);
  if (!(h.screen & SILLY)) { return; }

  p.text(14, 6, marble_s_blank31, NAME, SOLID);
  p.text(15, 6, marble_s_wrong, NAME, SOLID);
  p.text(16, 6, marble_s_blank31, NAME, SOLID);
}

void bonus(Grid &g, const Hud &h) {
  const Put p = {g, TOP_DX, 0};
  p.text(3, 3, marble_s_bonus_for, TEXT, SOLID);

  // value right aligned over the end of "TIME LEFT:       "
  char line[sizeof(marble_s_time_left)];
  memcpy(line, marble_s_time_left, sizeof(line));
  char v[16];
  commas(v, clampU(h.value, INT32_MAX));
  const int n = (int)strlen(v), len = (int)strlen(line);
  if (n <= len) { memcpy(line + len - n, v, n); }
  p.text(4, 3, line, TEXT, SOLID);
}

void out(Grid &g) {
  const Put p = {g, BANNER_DX, BANNER_DY};
  for (int row = 24; row <= 28; row += 2) { p.repeat(row, 13, BLANK_OUT, TEXT << 10 | SOLID); }
  p.text(25, 13, marble_s_out, TEXT, SOLID);
  p.text(27, 13, marble_s_over, TEXT, SOLID);
}

void finalScreen(Grid &g, const Hud &h) {
  const Put b = {g, BANNER_DX, BANNER_DY};
  const Put l = {g, LABEL_DX, BANNER_DY};
  const Put v = {g, VALUE_DX, BANNER_DY};
  const uint32_t sec = clampU(h.seconds, SECONDS_MAX);
  const uint8_t cong = sizeof(marble_big_congratulations[0]) / sizeof(uint16_t);
  char buf[16];

  b.tiles(2, 6, marble_big_congratulations[0], cong, BANNER);
  b.tiles(3, 6, marble_big_congratulations[1], cong, BANNER);
  b.text(7, 12, marble_s_you, NAME, CLEAR);
  b.text(8, 12, marble_s_the, NAME, CLEAR);

  // "BONUS FOR FINISHING     20,000": label and value move apart
  const char *fin = marble_s_bonus_fin;
  int value = (int)strlen(fin);
  while (value && (fin[value - 1] == ',' || (fin[value - 1] >= '0' && fin[value - 1] <= '9'))) { value--; }
  int label = value;
  while (label && fin[label - 1] == ' ') { label--; }
  l.text(13, 9, fin, label, NAME, CLEAR);
  v.text(13, 9 + value, fin + value, NAME, CLEAR);

  snprintf(buf, sizeof(buf), "%2u", (unsigned)sec);
  l.text(15, 10, buf, BANNER, CLEAR);
  l.text(15, 13, marble_s_sec, NAME, CLEAR);
  v.text(15, 33, commas(buf, sec * 1000, 6), NAME, CLEAR);

  // "DIED    TIMES x -1000    -": the '-' goes with the value
  const char *died = marble_s_died;
  int n = (int)strlen(died) - 1;
  const char *minus = died + n;
  while (n && died[n - 1] == ' ') { n--; }
  l.text(17, 7, died, n, NAME, CLEAR);
  snprintf(buf, sizeof(buf), "%2u", (unsigned)(h.deaths < SHOWN_MAX ? h.deaths : SHOWN_MAX));
  l.text(17, 12, buf, BANNER, CLEAR);
  v.text(17, 32, minus, NAME, CLEAR);
  v.text(17, 33, commas(buf, (h.deaths < DEATHS_MAX ? h.deaths : DEATHS_MAX) * 1000, 6), NAME, CLEAR);

  l.text(19, 23, marble_s_total, NAME, CLEAR);
  v.text(19, 32, commas(buf, clampU(h.value, INT32_MAX), 7), NAME, CLEAR);

  const Put f = {g, FINAL_DX, BANNER_DY};
  const uint8_t nf = sizeof(marble_big_final[0]) / sizeof(uint16_t);
  f.tiles(22, 4, marble_big_final[0], nf, BANNER);
  f.tiles(23, 4, marble_big_final[1], nf, BANNER);

  const Put s = {g, SCORE_DX, BANNER_DY};
  const uint8_t ns = sizeof(marble_big_score[0]) / sizeof(uint16_t);
  s.tiles(24, 12, marble_big_score[0], ns, BANNER);
  s.tiles(25, 12, marble_big_score[1], ns, BANNER);
  commas(buf, h.score);
  s.big(24, 40 - bigWidth(buf), buf, BANNER, CLEAR);
}

// title (MARBLE.md "Attract"): drawn over 3 frames as the arcade, table, copyright, coin line
static const int8_t COIN_DX = -2, CREDITS_DX = -7;
static const uint8_t TITLE_TABLE_ROW = 11, TABLE_COL = 13, SCORE_W = 9;

void title(Grid &g, const Hud &h, const marble_hiscore::Table &t) {
  const Put p = {g, BANNER_DX, BANNER_DY};
  p.text(9, 15, marble_s_trouble, NAME, CLEAR);
  for (uint8_t k = 0; k < marble_hiscore::ENTRIES; k++) {
    // " #1 C R   14,500": rank right aligned in 3, initials, score right aligned in 9
    char rank[8], score[16], line[32];
    snprintf(rank, sizeof(rank), "#%u", (unsigned)(k + 1));
    snprintf(line, sizeof(line), "%3s %.3s%s", rank, t.e[k].initials,
             commas(score, t.e[k].score, SCORE_W));
    p.text(TITLE_TABLE_ROW + k, TABLE_COL, line, BANNER, CLEAR);
  }
  if (h.stage < 1) { return; }

  p.text(29, 12, marble_s_copyright, BANNER, CLEAR);
  if (h.stage < 2) { return; }

  Put{g, COIN_DX, BANNER_DY}.text(28, 3, marble_s_coin, NAME, CLEAR);
  char credits[sizeof(marble_s_credits) + 8];
  snprintf(credits, sizeof(credits), "%s0    ", marble_s_credits);  // credits always 0
  Put{g, CREDITS_DX, BANNER_DY}.text(28, 24, credits, NAME, CLEAR);
}

// attract demo (MARBLE.md "Demo race"): coin box fitted from the arcade's 36 columns (lines
// colour 6, texts opaque), the banner of the BIOS message slot (string chain lines)
static const int8_t BOX_DX = -3, CREDITS_BOX_DX = -5;
static const uint8_t BOX_LINE = 6, BOX_LINE_W = 15, BOX_GAP_W = 6;

void demo(Grid &g, const Hud &h) {
  const Put p = {g, BOX_DX, BANNER_DY};
  const Put c = {g, CREDITS_BOX_DX, BANNER_DY};
  const uint16_t line = UNDERLINE | BOX_LINE << 10;
  p.repeat(27, 3, BOX_LINE_W, line);
  c.repeat(27, 24, BOX_LINE_W, line);
  p.text(28, 3, marble_s_coin, NAME, SOLID);
  p.repeat(28, 18, BOX_GAP_W, line);
  char credits[sizeof(marble_s_credits) + 8];
  snprintf(credits, sizeof(credits), "%s0    ", marble_s_credits);  // credits always 0
  c.text(28, 24, credits, NAME, SOLID);
  Put{g, BANNER_DX, BANNER_DY}.text(29, 12, marble_s_copyright, BANNER, SOLID);
  if (h.banner >= sizeof(marble_banners) / sizeof(marble_banners[0])) { return; }

  const MarbleBanner &b = marble_banners[h.banner];
  for (uint8_t k = 0; k < b.count; k++) {
    const MarbleBannerLine &l = marble_banner_lines[b.first + k];
    int x = l.col + BANNER_DX;  // 6 left as the other banners, kept inside the view
    if (x > COLS - MARBLE_BANNER_W) { x = COLS - MARBLE_BANNER_W; }
    if (x < 0) { x = 0; }
    Put{g, (int8_t)(x - l.col), BANNER_DY}.text(l.row, l.col, l.text, NAME, SOLID);
  }
}

// banner number of a message slot chain, NO_BANNER for none
uint8_t bannerOf(uint32_t chain) {
  for (uint8_t k = 0; k < sizeof(marble_banners) / sizeof(marble_banners[0]); k++) {
    if (chain && marble_banners[k].chain == chain) { return k; }
  }
  return marble_video::NO_BANNER;
}

void palette(Grid &g, const Hud &h) {
  if (!(h.screen & PAL)) {
    memset(g.pal, 0, sizeof(g.pal));
    return;
  }
  memcpy(g.pal, marble_hud_pal, sizeof(g.pal));
  if (h.screen & BONUS_PAL) {
    for (uint8_t i : BONUS_BLACK) { g.pal[i] = 0; }
    for (uint8_t i : BONUS_GREY) { g.pal[i] = MARBLE_HUD_BONUS_GREY; }
  }
  if (h.pal3 < 16) { g.pal[3] = marble_hud_pulse[h.pal3]; }
  if (h.pal11 < 16) { g.pal[11] = marble_hud_pulse[h.pal11]; }
}

// player fields read by update
static const uint8_t TIMER_F = 0x6a, FADE_F = 0x70, FLASH_F = 0x71, SCORE_F = 0xbc;
static const uint8_t DEATHS_F = 0xd2, SKIP_F = 0xd8, FADE_SHIFT = 2, FLASH_SHIFT = 1;
static const uint8_t PULSE_OFF = 0xff, SILLY_RACE = 4;
static const uint16_t BANNER_LEAD = 60;      // banner before the first +5 s
static const uint16_t PAL_LOAD = 4;          // race init to the palette load
static const uint16_t BONUS_FROM = 3;        // race end frame of the bonus line
static const uint16_t OUT_FROM = 2;          // time out frame of the box
static const uint16_t FINAL_FROM = 11;       // final frame of the texts
static const uint16_t TOP_AFTER = 2;         // game over frames with the top items (time out)
static const uint16_t FINAL_AFTER = 3;       // game over frames with the final screen
static const uint8_t TITLE_STAGES = 3;

int32_t clampS(int32_t v, int32_t hi) { return v > hi ? hi : v; }

}  // namespace

void update(Tracker &t, const marble_flow::Flow &f, const marble_physics::Mem &m, Hud &h) {
  const uint32_t P = marble_physics::PLAYER;
  const uint16_t ph = (uint16_t)marble_flow::phase(m);
  if (ph != t.phase) {
    t.before = t.phase;
    t.phase = ph;
    t.since = 0;
    t.bonusDone = false;
  }
  if (t.since < UINT16_MAX) { t.since++; }
  const uint16_t k = t.since;

  h = Hud{};
  h.banner = marble_video::NO_BANNER;
  h.score = m.rl(P + SCORE_F);
  h.seconds = m.sw(P + TIMER_F);
  h.deaths = (uint8_t)clampS(m.rw(P + DEATHS_F), UINT8_MAX);
  h.race = (uint8_t)m.rw(marble_physics::RACE);
  h.pal3 = h.pal11 = PULSE_OFF;
  uint16_t s = 0;

  // race intro: palette load, banner from 60 frames before the first +5 s
  if (f.intro == 0) { t.loaded = false; }
  if (f.intro != marble_flow::NO_INTRO) {
    const uint16_t first = marble_flow::firstAdd(f);
    const uint16_t lead = first > BANNER_LEAD ? first - BANNER_LEAD : 0;
    if (f.intro >= (lead < PAL_LOAD ? lead : PAL_LOAD)) { t.loaded = true; }
    if (f.intro < first) { t.base = h.seconds; }
    if (f.intro >= lead) {
      s |= INTRO | (h.race == SILLY_RACE ? SILLY : 0);
      h.add = (uint8_t)(marble_flow::raceTime(h.race) - (h.seconds - t.base));
    }
  }
  const bool shown = ph == marble_flow::RACE || ph == marble_flow::GAME_START ||
                     ph == marble_flow::RACE_END || ph == marble_flow::TIME_OUT;
  if (t.loaded && (shown || (ph == marble_flow::GAME_OVER && k < TOP_AFTER))) { s |= PAL | TOP; }

  // race end: bonus line until the bonus frame (0x118d2 sets +0x70 = 0 with the line, 0xff
  // with the points, also when there are none), then nothing until the next banner
  if (ph == marble_flow::RACE_END && !(s & INTRO)) {
    if (k >= BONUS_FROM && m.rb(P + FADE_F) == PULSE_OFF) { t.bonusDone = true; }
    if (t.bonusDone) {
      s = 0;
    } else if (k >= BONUS_FROM) {
      s |= BONUS | BONUS_PAL | PAL | TOP;
      h.value = clampS(h.seconds, SECONDS_MAX) * 100;
    }
  }
  if (ph == marble_flow::TIME_OUT && k >= OUT_FROM) { s |= OUT | PAL | TOP; }
  if (ph == marble_flow::FINAL) {
    s = PAL | (k >= FINAL_FROM ? FINAL : 0);
    h.value = f.final > 0 ? f.final : 0;
  }
  if (ph == marble_flow::GAME_OVER && t.before == marble_flow::FINAL && k < FINAL_AFTER) { s = PAL | FINAL; }
  if (ph == marble_flow::ATTRACT && marble_particles::count(m)) {  // the title
    s = PAL | TITLE;
    const uint16_t since = f.frame - marble_flow::TITLE_AT;
    h.stage = (uint8_t)(since < TITLE_STAGES ? since : TITLE_STAGES - 1);
  }
  if (ph == marble_flow::ATTRACT && f.demo == marble_flow::DEMO_LOAD) { s = BLACK; }
  if (ph == marble_flow::ATTRACT && f.demo >= marble_flow::DEMO_RUN) {  // ROM colours (MAME)
    s = PAL | TOP | DEMO;
    h.banner = bannerOf(m.rl(marble_physics::MSG_CHAIN));
  }

  // palette pulses: colours reload with the palette; the counters step before the read
  if (!(s & PAL) || !(t.screen & PAL)) { t.pal3 = t.pal11 = PULSE_OFF; }
  const uint8_t fade = m.rb(P + FADE_F), flash = m.rb(P + FLASH_F);
  if (fade && fade != PULSE_OFF && !m.rb(P + SKIP_F)) { t.pal3 = ((fade - 1) & 0x3f) >> FADE_SHIFT; }
  if (flash == PULSE_OFF) {
    t.pal11 = PULSE_OFF;
  } else if (flash) {
    t.pal11 = ((flash - 1) & 0x1f) >> FLASH_SHIFT;
  }
  h.pal3 = t.pal3;
  h.pal11 = t.pal11;
  if (s & DEMO) { h.pal3 = h.pal11 = PULSE_OFF; }
  h.screen = s;
  t.screen = s;
}

void build(Grid &g, const Hud &h, const marble_hiscore::Table &t) {
  if (g.valid && !memcmp(&g.shown, &h, sizeof(h))) { return; }
  g.valid = true;
  g.shown = h;
  memset(g.cells, 0, sizeof(g.cells));
  memset(g.used, 0, sizeof(g.used));
  palette(g, h);

  // arcade drawing order: the bonus line covers a timer border cell
  if (h.screen & TOP) { top(g, h); }
  if (h.screen & INTRO) { intro(g, h); }
  if (h.screen & BONUS) { bonus(g, h); }
  if (h.screen & OUT) { out(g); }
  if (h.screen & FINAL) { finalScreen(g, h); }
  if (h.screen & TITLE) { title(g, h, t); }
  if (h.screen & DEMO) { demo(g, h); }
}

void blend(const Grid &g, int row, uint16_t *dst) {
  if (!(g.shown.screen & PAL) || row < 0 || row >= ROWS || !g.used[row]) { return; }

  static const uint16_t NONE[8] = {};
  for (int c = 0; c < COLS; c++) {
    const uint16_t w = g.cells[row][c];
    if (!w) { continue; }
    const uint16_t code = w & 0x3ff;
    if (code >= MARBLE_HUD_CODES) { continue; }
    const uint8_t gi = marble_hud_glyph()[code];
    const uint16_t *lines = gi == MARBLE_HUD_NO_GLYPH ? NONE : marble_hud_glyphs()[gi];
    if (lines == NONE && !(w & OPAQUE)) { continue; }

    const uint16_t *pal = g.pal + ((w >> 10) & 7) * 4;
    uint16_t *out = dst + c * 8;
    for (int y = 0; y < 8; y++, out += marble_video::OUT_W) {
      const uint16_t bits = lines[y];
      for (int x = 0; x < 8; x++) {
        const uint8_t pen = (bits >> (14 - 2 * x)) & 3;
        if (pen || (w & OPAQUE)) { out[x] = pal[pen]; }
      }
    }
  }
}

}  // namespace marble_hud

#endif  // GAME_MARBLE
