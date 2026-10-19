#include "system1_collision.h"

#include <string.h>

using namespace system1_video;

namespace {

// Bit fgPri | bgPri << 2 set if the lookup PROM collides an opaque sprite
// pixel with that tile priority pair for any fg/bg opacity. pr-5317: only
// priority 2-3 tiles collide, so units over other tiles skip the pixel lookups.
uint16_t collidingPairs(const uint8_t *prom) {
  uint16_t pairs = 0;
  for (int p = 0; p < MIX_PRIORITIES; p++) {
    for (int o = 0; o < 4; o++) {
      const uint8_t idx = mixIndex(true, !(o & 1), (p & 3) << 6, !(o & 2), (p >> 2) << 6);
      if (!(prom[idx] & MIX_NO_COLLIDE)) {
        pairs |= 1 << p;
      }
    }
  }
  return pairs;
}

constexpr int PAGE_ROWS = PAGE_BYTES / 2 / PAGE_COLS;
constexpr int TILE_PRIORITIES = 4;  // per layer, MIX_PRIORITIES pairs

// Bit p of pris[row] set if a tile of mixer priority p is in that tile row
void rowPriorities(const uint8_t *page, uint8_t *pris) {
  for (int r = 0; r < PAGE_ROWS; r++) {
    uint8_t m = 0;
    for (int c = 0; c < PAGE_COLS; c++) {
      m |= 1 << (tileAt(page, r * PAGE_COLS + c).color >> 6);
    }
    pris[r] = m;
  }
}

// bgFor[fg priority mask] = bg priorities that collide with any of them
void collidingBg(uint16_t pairs, uint8_t *bgFor) {
  for (int m = 0; m < MIX_PRIORITIES; m++) {
    bgFor[m] = 0;
    for (int fp = 0; fp < TILE_PRIORITIES; fp++) {
      if (!(m & (1 << fp))) {
        continue;
      }
      for (int bp = 0; bp < TILE_PRIORITIES; bp++) {
        bgFor[m] |= ((pairs >> (fp | (bp << 2))) & 1) << bp;
      }
    }
  }
}

// One tile layer on one line: the tile is looked up once per 8 pixels.
// pages: left and right page of the line, x 0..511 (fg: x < 256).
struct LayerRow {
  const uint8_t *const *pages;
  const TileSet &tiles;
  int row;    // tile row offset in page
  uint8_t y;
  int col = -1;
  const uint32_t *gfx = nullptr;
  uint8_t color = 0;

  LayerRow(const uint8_t *const *p, const TileSet &t, uint16_t y)
      : pages(p), tiles(t), row(((y % PAGE_SIZE) >> 3) * PAGE_COLS), y(y) {}

  // tile under pixel x, returns its mixer priority
  inline __attribute__((always_inline)) uint8_t tile(uint16_t x) {
    if ((x >> 3) != col) {
      col = x >> 3;
      const Tile t = tileAt(pages[x / PAGE_SIZE], row + (col & (PAGE_COLS - 1)));
      gfx = tiles[t.code];
      color = t.color;
    }
    return color >> 6;
  }

  // pixel x of the tile selected by tile(x)
  inline __attribute__((always_inline)) uint8_t pixel(uint16_t x) const {
    return tiles.pixel(gfx, x, y);
  }
};

// pris[p][row] = rowPriorities of bg page p; pages repeat on System 1
void bgPriorities(const uint8_t *const *pages, uint8_t pris[BG_PAGES][PAGE_ROWS]) {
  for (int p = 0; p < BG_PAGES; p++) {
    int same = 0;
    while (pages[same] != pages[p]) {
      same++;
    }
    if (same < p) {
      memcpy(pris[p], pris[same], PAGE_ROWS);
      continue;
    }
    rowPriorities(pages[p], pris[p]);
  }
}

} // namespace

void Collision::reset() {
  memset(mix, 0, sizeof(mix));
  memset(spr, 0, sizeof(spr));
  memset(occupied, 0, sizeof(occupied));
  mixSummary = 0;
  sprSummary = 0;
}

void Collision::scan(const Frame &f) {
  // MAME skips the whole screen update while blanked
  if (f.videoMode & VIDEO_BLANK) {
    return;
  }

  Sprite list[SPRITES];
  const int count = decodeSprites(f.spriteRam, f.spriteBanks, list, spriteXOffset(f.board));
  Layers layers;
  readLayers(f.vram, f.board, layers);
  const uint16_t pairs = collidingPairs(f.prom);

  const uint8_t *fg = f.vram + fgPageOf(f.board) * PAGE_BYTES;
  const uint8_t *bgPages[BG_PAGES];
  for (int p = 0; p < BG_PAGES; p++) {
    bgPages[p] = f.vram + layers.bgPage[p] * PAGE_BYTES;
  }

  // lines whose fg and bg tile rows hold no colliding priority pair skip
  // the mixer (SWAT: every line)
  uint8_t fgPris[PAGE_ROWS], bgPris[BG_PAGES][PAGE_ROWS], bgFor[MIX_PRIORITIES];
  rowPriorities(fg, fgPris);
  bgPriorities(bgPages, bgPris);
  collidingBg(pairs, bgFor);

  for (int y = 0; y < VIS_LINES; y++) {
    drawLine(f, list, count, y);

    const uint16_t by = bgMapY(y, layers.scrollY);
    const int pageRow = (by / PAGE_SIZE) * 2;
    const int row = (by % PAGE_SIZE) >> 3;
    if (!(bgFor[fgPris[y >> 3]] & (bgPris[pageRow][row] | bgPris[pageRow + 1][row]))) {
      memset(occupied, 0, sizeof(occupied));
      continue;
    }
    mixLine(f, y, &bgPages[pageRow], by, layers.rowScroll[(y >> 3) & (SCROLL_ROWS - 1)], pairs);
  }
}

// Line buffer as the hardware: a pixel over an opaque one collides.
__attribute__((flatten)) void Collision::drawLine(const Frame &f, const Sprite *list, int count, int y) {
  const int lineX0 = 2 * f.vis.x0;
  const int lineX1 = 2 * f.vis.x1 + 1;

  for (int n = 0; n < count; n++) {
    const Sprite &s = list[n];
    if (y < s.top || y >= s.bottom) {
      continue;
    }

    auto unit = [&](int x) {
      if (x < lineX0 || x > lineX1) {
        return;
      }

      uint32_t &word = occupied[x >> 5];
      const uint32_t bit = 1u << (x & 31);
      if (word & bit) {
        spr[line[x] + SPRITES * n] = 1;
        sprSummary = 1;
      }
      line[x] = n;
      word |= bit;
    };

    const uint8_t *bank = f.spriteRom + s.bank * SPRITE_BANK;
    walkRow(bank, rowAddr(s, y), [&](int i, uint8_t c) {
      if (c == 0) {
        return;
      }

      // both units of the pixel at once, unless clipped or split over two words
      const int x = s.x + 2 * i;
      if (x < lineX0 || x + 1 > lineX1 || (x & 31) == 31) {
        unit(x);
        unit(x + 1);
        return;
      }

      uint32_t &word = occupied[x >> 5];
      const int b = x & 31;
      const uint32_t hit = (word >> b) & 3;
      if (hit) {
        if (hit & 1) {
          spr[line[x] + SPRITES * n] = 1;
        }
        if (hit & 2) {
          spr[line[x + 1] + SPRITES * n] = 1;
        }
        sprSummary = 1;
      }
      line[x] = n;
      line[x + 1] = n;
      word |= 3u << b;
    });
  }
}

// Mixer PROM under each opaque sprite pixel; empties the line buffer behind it.
// bgPages: left and right page of the bg line by, with its row's scroll.
__attribute__((flatten)) void Collision::mixLine(const Frame &f, int y,
                                                 const uint8_t *const *bgPages, uint16_t by,
                                                 int scroll, uint16_t pairs) {
  // fg page twice: LayerRow's left and right page
  const uint8_t *fgPage = f.vram + fgPageOf(f.board) * PAGE_BYTES;
  const uint8_t *fgPages[2] = {fgPage, fgPage};
  LayerRow fg(fgPages, f.tiles, y);
  LayerRow bg(bgPages, f.tiles, by);

  for (int w = 0; w < LINE_WORDS; w++) {
    uint32_t bits = occupied[w];
    occupied[w] = 0;

    while (bits) {
      const int x = w * 32 + __builtin_ctz(bits);
      bits &= bits - 1;

      const uint8_t fx = (uint8_t)(x / 2);
      const uint16_t bx = bgMapX(x, scroll);
      if (!((pairs >> (fg.tile(fx) | (bg.tile(bx) << 2))) & 1)) {
        continue;
      }

      const uint8_t fp = fg.pixel(fx);
      const uint8_t bp = bg.pixel(bx);
      const uint8_t v = f.prom[mixIndex(true, fp, fg.color, bp, bg.color)];
      if (v & MIX_NO_COLLIDE) {
        continue;
      }

      mix[((v & MIX_BANK) << 2) | line[x]] = 1;
      mixSummary = 1;
    }
  }
}
