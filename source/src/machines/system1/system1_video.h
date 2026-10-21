#ifndef SYSTEM1_VIDEO_H
#define SYSTEM1_VIDEO_H

#include <stdint.h>
#include <string.h>

// Pure Sega System 1 video logic (MAME system1_v.cpp), shared by render and
// collision. x512 = MAME screen x (2x pixel clock), x = half-res native x,
// y = native line. ROT270 onto galagino: row = 271 - x, column = y.
namespace system1_video {

constexpr int VIS_LINES = 224;
constexpr int ROW_OF_X0 = 271;   // ROT270: half-res x 0 -> galagino row 271
constexpr int ROT0_WIDTH = 240;     // ROT0: panel columns, a window into 256
constexpr int ROT0_FIRST_ROW = 32;  // ROT0: line 0 -> galagino row 32, centered in 288
constexpr int SPRITES = 32;
constexpr int SPRITE_BYTES = 16;
constexpr int SPRITE_BANK = 0x8000;
constexpr int MAX_ROW_BYTES = 128;  // 512 units / 4 per byte: end of the line buffer
constexpr int PAGE_COLS = 32;
constexpr int PAGE_BYTES = 0x800;
constexpr int PENS_PER_SPRITE = 16;
constexpr uint8_t SPRITE_LIST_END = 0xff;
constexpr uint8_t TRANSPARENT_ROW_END = 0x0f;
constexpr uint8_t VIDEO_BLANK = 0x10;     // PPI port B bit 4
constexpr uint16_t SCROLL_X_LO = 0xffc;   // VRAM offsets
constexpr uint16_t SCROLL_X_HI = 0xffd;
constexpr uint16_t SCROLL_Y = 0xfbd;
constexpr int SCROLL_X_BIAS = 28;
constexpr uint8_t MIX_SOURCE = 0x03;      // lookup PROM: 0 sprite, 1 fg, 2/3 bg
constexpr uint8_t SOURCE_SPRITE = 0;
constexpr uint8_t SOURCE_FG = 1;
constexpr uint8_t MIX_NO_COLLIDE = 0x04;
constexpr uint8_t MIX_BANK = 0x08;
constexpr uint16_t FG_PENS = 0x200;       // palette RAM blocks: sprite, fg, bg
constexpr uint16_t BG_PENS = 0x400;
constexpr int TILE_SIZE = 8;
constexpr int MIX_PRIORITIES = 16;        // fg x bg tile priority (color bits 6-7)

// bg map 512 x 512: pages [0 1; 2 3], x scroll per 8 lines
constexpr int BG_PAGES = 4;
constexpr int SCROLL_ROWS = 32;
constexpr int BG_MAP_MASK = 0x1ff;
constexpr int PAGE_SIZE = 256;            // pixels per page side
constexpr uint8_t SYS1_FG_PAGE = 1;       // System 1: bg is page 0
constexpr uint8_t SYS2_FG_PAGE = 0;

// System 2 registers in page 0 (MAME screen_update_system2_rowscroll)
constexpr uint16_t SYS2_PAGE_SELECT = 0x740;  // 4 selects, every other byte
constexpr uint16_t SYS2_SCROLL_Y = 0x7ba;
constexpr uint16_t SYS2_ROW_SCROLL = 0x7c0;   // per row: 9 bit, little endian
constexpr int SYS2_ROW_SCROLL_MASK = 0x1ff;
constexpr int SYS2_ROW_SCROLL_BIAS = 10 - 512;
constexpr int SYS2_SPRITE_X_OFFSET = 14;      // 512 units, not flipped

// The CPU window e000-efff holds pages 0 and 1. MAME selects 8 pages, but
// Choplifter never banks VRAM and selects page 1 only (MAME probe).
constexpr uint8_t WINDOW_PAGE_MASK = 1;

// visible half-res x range, 512 units 2 * x0 .. 2 * x1 + 1
struct Visarea {
  int16_t x0;
  int16_t x1;
};
constexpr Visarea VISAREA_SCROLL = {8, 247};  // sys1ppis: 16..495, rows 24..263
constexpr Visarea VISAREA_FULL = {0, 255};    // sys1ppi: 0..511, rows 16..271

inline int rowOfX(int x) { return ROW_OF_X0 - x; }
inline int xOfRow(int row) { return ROW_OF_X0 - row; }

inline int bgScroll(uint8_t lo, uint8_t hi) {
  return (int16_t)(lo | (hi << 8)) + SCROLL_X_BIAS;
}

// all four bg pages are page 0, so the 512 wide map repeats every 256
inline uint8_t bgCol(int x512, int scroll) { return (uint8_t)((x512 - scroll) / 2); }
inline uint8_t bgY(int y, uint8_t yscroll) { return (uint8_t)(y + yscroll); }

// position in the 512 x 512 bg map (MAME video_update_common)
inline uint16_t bgMapX(int x512, int scroll) { return ((x512 - scroll) / 2) & BG_MAP_MASK; }
inline uint16_t bgMapY(int y, uint8_t yscroll) { return (y + yscroll) & BG_MAP_MASK; }

// System 1 board or System 2 (sys2row: own bg pages, row scroll)
enum VideoBoard : uint8_t { VIDEO_SYSTEM1, VIDEO_SYSTEM2 };

inline uint8_t fgPageOf(VideoBoard b) { return b == VIDEO_SYSTEM2 ? SYS2_FG_PAGE : SYS1_FG_PAGE; }

inline int spriteXOffset(VideoBoard b) { return b == VIDEO_SYSTEM2 ? SYS2_SPRITE_X_OFFSET : 0; }

// Tile layer registers of one frame, pages as window page numbers. System 1
// (MAME screen_update_system1): bg page 0 everywhere, one x scroll.
struct Layers {
  uint8_t bgPage[BG_PAGES];
  uint8_t scrollY;
  int16_t rowScroll[SCROLL_ROWS];  // 512 units
};

inline void readLayers(const uint8_t *vram, VideoBoard b, Layers &l) {
  if (b == VIDEO_SYSTEM1) {
    // 16 bit as MAME's (s16) cast of the biased scroll
    const int16_t scroll = (int16_t)bgScroll(vram[SCROLL_X_LO], vram[SCROLL_X_HI]);
    memset(l.bgPage, 0, sizeof(l.bgPage));
    l.scrollY = vram[SCROLL_Y];
    for (int r = 0; r < SCROLL_ROWS; r++) {
      l.rowScroll[r] = scroll;
    }
    return;
  }

  for (int p = 0; p < BG_PAGES; p++) {
    l.bgPage[p] = vram[SYS2_PAGE_SELECT + 2 * p] & WINDOW_PAGE_MASK;
  }
  l.scrollY = vram[SYS2_SCROLL_Y];
  for (int r = 0; r < SCROLL_ROWS; r++) {
    const uint8_t *reg = &vram[SYS2_ROW_SCROLL + 2 * r];
    l.rowScroll[r] = ((reg[0] | (reg[1] << 8)) & SYS2_ROW_SCROLL_MASK) + SYS2_ROW_SCROLL_BIAS;
  }
}

struct Tile {
  uint16_t code;
  uint8_t color;  // bits 6-7 = mixer priority
};

inline __attribute__((always_inline)) Tile tileAt(const uint8_t *page, int index) {
  const uint16_t d = page[2 * index] | (page[2 * index + 1] << 8);
  Tile t;
  t.code = ((d >> 4) & 0x800) | (d & 0x7ff);
  t.color = (d >> 5) & 0xff;
  return t;
}

// romconv tile word order: ROT270 games render along native columns, ROT0
// games along native lines, so each stores the words its renderer walks
enum TileLayout : uint8_t {
  TILES_BY_COLUMN,  // word per pixel column, row 0 in bits 23..21
  TILES_BY_ROW,     // word per pixel row, column 0 in bits 23..21
};

// Tile gfx deduplicated by romconv: code -> unique tile via map
struct TileSet {
  const uint32_t (*gfx)[8];
  const uint16_t *map;
  uint16_t mask;      // code mask, map size - 1
  TileLayout layout;  // omitted in an initializer: TILES_BY_COLUMN
  inline __attribute__((always_inline)) const uint32_t *operator[](uint16_t code) const {
    return gfx[map[code & mask]];
  }

  // pixel (x, y) of one tile's words
  inline __attribute__((always_inline)) uint8_t pixel(const uint32_t *w, uint8_t x,
                                                      uint8_t y) const {
    if (layout == TILES_BY_ROW) {
      return (w[y & 7] >> (3 * (7 - (x & 7)))) & 7;
    }
    return (w[x & 7] >> (3 * (7 - (y & 7)))) & 7;
  }
};

// palette RAM index within the layer's 0x200 block
inline uint16_t tilePen(uint8_t color, uint8_t pix) { return ((color & 0x3f) << 3) | pix; }

inline uint8_t mixIndex(bool sprOpaque, uint8_t fgPix, uint8_t fgColor, uint8_t bgPix,
                        uint8_t bgColor) {
  return (sprOpaque ? 0 : 0x01) | (fgPix ? 0 : 0x02) | ((fgColor >> 6) << 2) |
         (bgPix ? 0 : 0x10) | ((bgColor >> 6) << 5);
}

inline uint8_t tilePixel(const uint8_t *page, const TileSet &tiles, uint8_t x, uint8_t y,
                         uint8_t &color) {
  const Tile t = tileAt(page, (y >> 3) * PAGE_COLS + (x >> 3));
  color = t.color;
  return tiles.pixel(tiles[t.code], x, y);
}

// Lookup PROM source (0 sprite, 1 fg, 2/3 bg) per tile priority pair p =
// fgPri | bgPri << 2, 2 bits for each opacity case o: bit 0 sprite
// transparent, bit 1 fg transparent, bit 2 bg transparent. Returns true if
// a pixel without fg and sprite always shows bg (o = 3 and 7), as in pr-5317:
// renderLine then skips tiles without fg and sprite pixels.
inline bool buildMixSel(const uint8_t *prom, uint16_t *sel) {
  bool clearShowsBg = true;
  for (int p = 0; p < MIX_PRIORITIES; p++) {
    sel[p] = 0;
    for (int o = 0; o < 8; o++) {
      const uint8_t idx = (o & 3) | ((o & 4) << 2) | ((p & 3) << 2) | ((p >> 2) << 5);
      sel[p] |= (prom[idx] & MIX_SOURCE) << (2 * o);
    }
    clearShowsBg = clearShowsBg && ((sel[p] >> 6) & (sel[p] >> 14) & 2);
  }
  return clearShowsBg;
}

// Per frame inputs of renderLine / renderRow
struct LineSrc {
  const uint8_t *fgPage;
  const uint8_t *bgPages[BG_PAGES];  // System 1: all the same page
  TileSet tiles;
  const uint16_t *pal;
  const uint16_t *mixSel;     // buildMixSel
  bool clearShowsBg;          // buildMixSel
  const int16_t *rowScroll;   // Layers::rowScroll
  uint8_t scrollY;
};

// pixels with fixed fg and bg tile, tile words aligned to bit 21
struct Run {
  uint32_t fw;
  uint32_t bw;
  const uint16_t *fgPal;
  const uint16_t *bgPal;
  uint16_t sel;
};

// bg tile under line pixel y: palette, priority and word aligned to y
inline __attribute__((always_inline)) const uint16_t *bgTile(const LineSrc &f, uint8_t bx,
                                                              uint8_t by, uint32_t &bw,
                                                              int &pri) {
  const Tile t = tileAt(f.bgPages[0], (by >> 3) * PAGE_COLS + (bx >> 3));
  bw = f.tiles[t.code][bx & 7] << (3 * (by & 7));
  pri = t.color >> 6;
  return &f.pal[BG_PENS | tilePen(t.color, 0)];
}

__attribute__((noinline)) inline void mixRun(uint16_t *out, const uint16_t *pens, int n,
                                             const Run &r, const uint16_t *sprPal) {
  uint32_t fw = r.fw;
  uint32_t bw = r.bw;
  for (int i = 0; i < n; i++, fw <<= 3, bw <<= 3) {
    const uint8_t fp = (fw >> 21) & 7;
    const uint8_t bp = (bw >> 21) & 7;
    const uint16_t spr = pens[i];
    const int o = (spr ? 0 : 1) | (fp ? 0 : 2) | (bp ? 0 : 4);
    const uint8_t source = (r.sel >> (2 * o)) & MIX_SOURCE;
    if (source == SOURCE_SPRITE) {
      out[i] = sprPal[spr];
    } else if (source == SOURCE_FG) {
      out[i] = r.fgPal[fp];
    } else {
      out[i] = r.bgPal[bp];
    }
  }
}

// One output line at half-res x: fg + bg tiles and the sprite pens already in
// out through the lookup PROM (MAME video_update_common). sprTiles bit ty
// marks fg tile rows ty that may hold sprite pens, all others are 0. Pass 1
// paints bg, pass 2 mixes only the fg tiles with fg or sprite pixels, few in
// practice. ROT270 games are all System 1: one bg page, one scroll.
inline void renderLine(const LineSrc &f, int x, uint32_t sprTiles, uint16_t *out) {
  static const uint16_t NO_PENS[TILE_SIZE] = {};
  const uint8_t bx = bgCol(2 * x, f.rowScroll[0]);

  // pass 1 overwrites the sprite pens: keep the tiles holding them
  uint16_t pens[VIS_LINES];
  if (sprTiles) {
    const int lo = __builtin_ctz(sprTiles) * TILE_SIZE;
    const int hi = (32 - __builtin_clz(sprTiles)) * TILE_SIZE;
    memcpy(&pens[lo], &out[lo], (hi - lo) * sizeof(uint16_t));
  }

  uint8_t by = f.scrollY;
  for (int y = 0; y < VIS_LINES;) {
    uint32_t bw;
    int pri;
    const uint16_t *bgPal = bgTile(f, bx, by, bw, pri);
    int n = TILE_SIZE - (by & 7);
    n = y + n > VIS_LINES ? VIS_LINES - y : n;
    for (int i = 0; i < n; i++, bw <<= 3) {
      out[y + i] = bgPal[(bw >> 21) & 7];
    }
    y += n;
    by += n;
  }

  for (int ty = 0; ty < VIS_LINES / TILE_SIZE; ty++) {
    const int y = ty * TILE_SIZE;
    const Tile t = tileAt(f.fgPage, ty * PAGE_COLS + (x >> 3));
    Run r;
    r.fw = f.tiles[t.code][x & 7];
    const bool sprite = (sprTiles >> ty) & 1;
    if (r.fw == 0 && !sprite && f.clearShowsBg) {
      continue;
    }

    // up to two bg tiles under this fg tile
    r.fgPal = &f.pal[FG_PENS | tilePen(t.color, 0)];
    const int fgPri = t.color >> 6;
    const uint16_t *tilePens = sprite ? &pens[y] : NO_PENS;
    for (int i = 0; i < TILE_SIZE;) {
      const uint8_t by = f.scrollY + y + i;
      int bgPri;
      r.bgPal = bgTile(f, bx, by, r.bw, bgPri);
      r.sel = f.mixSel[fgPri | (bgPri << 2)];
      int n = TILE_SIZE - (by & 7);
      n = i + n > TILE_SIZE ? TILE_SIZE - i : n;
      mixRun(&out[y + i], &tilePens[i], n, r, f.pal);
      r.fw <<= 3 * n;
      i += n;
    }
  }
}

// One ROT0 output line: native line y into out[0..n), bg and sprites from
// native x0 + i, fg from fgX0 + i (HUD lines keep their text at a fixed x
// while the scenery under it moves with the window; else fgX0 == x0). Tiles
// stored TILES_BY_ROW. Sprite pens are already in out; sprCols bit tx marks
// native tile columns tx (sprite space) that may hold them. One pass over
// runs with a fixed fg and bg tile: runs without fg or sprite pixels copy
// bg, the others go through mixRun (renderLine's pass 2). The bg line
// crosses the left and right page of its page row and has its row's scroll.
inline void renderRow(const LineSrc &f, int y, int x0, int fgX0, int n, uint32_t sprCols,
                      uint16_t *out) {
  static const uint16_t NO_PENS[TILE_SIZE] = {};
  constexpr uint32_t ROW_BITS = 0xffffff;  // 8 pixels x 3 bits
  const int scroll = f.rowScroll[(y >> 3) & (SCROLL_ROWS - 1)];
  const uint16_t by = bgMapY(y, f.scrollY);
  const uint8_t *const *bgPages = &f.bgPages[(by / PAGE_SIZE) * 2];
  const int fgRow = (y >> 3) * PAGE_COLS;
  const int bgRow = ((by % PAGE_SIZE) >> 3) * PAGE_COLS;

  // bgMapX truncates toward 0: with an odd scroll bg column 0 shows twice,
  // the second time at i = dup, where the column count restarts
  const int dup = (scroll & 1) ? (scroll + 1) / 2 - x0 : -1;

  int fgCol = -1;
  int bgTileCol = -1;
  uint32_t fgWord = 0;
  uint32_t bgWord = 0;
  int fgPri = 0;
  int bgPri = 0;
  Run r;
  for (int i = 0; i < n;) {
    const uint16_t bx = bgMapX(2 * (x0 + i), scroll);
    const int fx = fgX0 + i;
    int len = TILE_SIZE - (bx & 7);
    len = TILE_SIZE - (fx & 7) < len ? TILE_SIZE - (fx & 7) : len;
    len = n - i < len ? n - i : len;
    len = dup > i && dup < i + len ? dup - i : len;

    // tiles change every 8 pixels, runs are shorter
    if ((fx >> 3) != fgCol) {
      fgCol = fx >> 3;
      const Tile t = tileAt(f.fgPage, fgRow + fgCol);
      fgWord = f.tiles[t.code][y & 7];
      r.fgPal = &f.pal[FG_PENS | tilePen(t.color, 0)];
      fgPri = t.color >> 6;
    }
    if ((bx >> 3) != bgTileCol) {
      bgTileCol = bx >> 3;
      const Tile t = tileAt(bgPages[bx / PAGE_SIZE], bgRow + (bgTileCol & (PAGE_COLS - 1)));
      bgWord = f.tiles[t.code][by & 7];
      r.bgPal = &f.pal[BG_PENS | tilePen(t.color, 0)];
      bgPri = t.color >> 6;
    }
    r.fw = fgWord << (3 * (fx & 7));
    r.bw = bgWord << (3 * (bx & 7));

    // with the fg shifted (HUD lines) sprite tile columns do not line up,
    // so any sprite on the line counts for all
    const bool sprite = fgX0 == x0 ? (sprCols >> fgCol) & 1 : sprCols != 0;
    if ((r.fw & ROW_BITS) == 0 && !sprite && f.clearShowsBg) {
      uint32_t bw = r.bw;
      for (int k = 0; k < len; k++, bw <<= 3) {
        out[i + k] = r.bgPal[(bw >> 21) & 7];
      }
    } else {
      r.sel = f.mixSel[fgPri | (bgPri << 2)];
      mixRun(&out[i], sprite ? &out[i] : NO_PENS, len, r, f.pal);
    }

    i += len;
  }
}

struct Sprite {
  int16_t top;     // first line
  int16_t bottom;  // one past the last line
  int16_t x;       // 512 units
  uint16_t stride;
  uint16_t addr;   // row y reads from addr + (y - top + 1) * stride
  uint8_t bank;
};

// Sprites 0..n-1; entry[0] == 0xff hides it and all later ones. xOffset:
// spriteXOffset, omitted: System 1.
inline int decodeSprites(const uint8_t *ram, uint8_t banks, Sprite *out, int xOffset = 0) {
  for (int n = 0; n < SPRITES; n++) {
    const uint8_t *e = ram + n * SPRITE_BYTES;
    if (e[0] == SPRITE_LIST_END) {
      return n;
    }

    Sprite &s = out[n];
    s.top = e[0] + 1;
    s.bottom = e[1] + 1;
    s.x = ((e[2] | (e[3] << 8)) & 0x1ff) + xOffset;
    s.stride = e[4] | (e[5] << 8);
    s.addr = e[6] | (e[7] << 8);
    s.bank = (((e[3] & 0x80) >> 7) | ((e[3] & 0x40) >> 5) | ((e[3] & 0x20) >> 3)) % banks;
  }
  return SPRITES;
}

inline uint16_t rowAddr(const Sprite &s, int y) {
  return (uint16_t)(s.addr + (y - s.top + 1) * s.stride);
}

// half-res x of pixel 0, sampled at even 512 positions like the tile layers
inline int spriteX(int x512) { return (x512 + 1) >> 1; }

// Calls px(i, nibble) for each pixel of one row until a 0x0f nibble. Pixel i
// covers 512 units x + 2i and x + 2i + 1. Returns the pixel count.
template <class F> inline int walkRow(const uint8_t *bank, uint16_t addr, F px) {
  const int delta = (addr & 0x8000) ? -1 : 1;
  uint16_t cur = addr;
  int i = 0;
  for (int n = 0; n < MAX_ROW_BYTES; n++, cur += delta) {
    const uint8_t d = bank[cur & 0x7fff];
    const bool mirrored = cur & 0x8000;
    const uint8_t first = mirrored ? (d & 0x0f) : (d >> 4);
    const uint8_t second = mirrored ? (d >> 4) : (d & 0x0f);
    if (first == TRANSPARENT_ROW_END) {
      return i;
    }
    px(i++, first);

    if (second == TRANSPARENT_ROW_END) {
      return i;
    }
    px(i++, second);
  }
  return i;
}

// ROT0 window into the 256 px line. The game's own content may not span all
// 256 px (Pitfall II: x 5..249), so the window moves only within [min, max],
// following the player sprite. HUD lines use a fixed window: their text stays
// whole while the playfield below moves.
constexpr int8_t NO_PLAYER = -1;
constexpr int PLAYER_X_MIN = 8;
constexpr int PLAYER_X_MAX = 248;

struct Window {
  int8_t min;           // native x of panel column 0
  int8_t max;
  int8_t playerSprite;  // sprite slot of the player, NO_PLAYER: centered
  uint8_t hudLines;     // native lines 0 .. hudLines - 1 at hudX
  int8_t hudX;
};

// proportional: player at the left wall -> min, right wall -> max
inline int windowTarget(const Window &w, int playerX) {
  if (playerX < 0) {
    return (w.min + w.max) / 2;
  }
  if (playerX <= PLAYER_X_MIN) {
    return w.min;
  }
  if (playerX >= PLAYER_X_MAX) {
    return w.max;
  }
  return w.min + (playerX - PLAYER_X_MIN) * (w.max - w.min) / (PLAYER_X_MAX - PLAYER_X_MIN);
}

// 1 px per frame toward the target
inline int easeWindow(int cur, int target) {
  return cur < target ? cur + 1 : cur > target ? cur - 1 : cur;
}

// The player sprite blinks or vanishes for a few frames (hit, respawn):
// keep its last target, so the window does not shake toward the center.
constexpr int PLAYER_LOST_FRAMES = 30;

struct WindowFollow {
  int last;     // target while the player was last seen
  int missing;  // frames without the player, 0 = none seen yet

  int target(const Window &w, int playerX) {
    if (playerX >= 0) {
      last = windowTarget(w, playerX);
      missing = 1;
      return last;
    }
    if (missing == 0 || missing > PLAYER_LOST_FRAMES) {
      return windowTarget(w, NO_PLAYER);
    }
    missing++;
    return last;
  }
};

// ROT0: pens of one sprite on native line y into out[0..n), out[i] = native
// x winX + i. x0 = spriteX of the sprite. Returns the native fg tile columns
// it wrote (renderRow sprCols).
inline uint32_t spriteRow(const uint8_t *bank, const Sprite &s, int x0, uint16_t penBase,
                          int y, int winX, int n, uint16_t *out) {
  if (y < s.top || y >= s.bottom) {
    return 0;
  }

  uint32_t cols = 0;
  walkRow(bank, rowAddr(s, y), [&](int i, uint8_t c) {
    const int col = x0 + i - winX;
    if (c == 0 || col < 0 || col >= n) {
      return;
    }
    out[col] = penBase | c;
    cols |= 1u << ((x0 + i) >> 3);
  });
  return cols;
}

} // namespace system1_video

#endif
