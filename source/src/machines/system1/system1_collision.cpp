#include "system1_collision.h"

#include <string.h>

using namespace system1_video;

void Collision::reset() {
  memset(mix, 0, sizeof(mix));
  memset(spr, 0, sizeof(spr));
  memset(line, EMPTY, sizeof(line));
  mixSummary = 0;
  sprSummary = 0;
}

void Collision::scan(const Frame &f) {
  // MAME skips the whole screen update while blanked
  if (f.videoMode & VIDEO_BLANK) {
    return;
  }

  Sprite list[SPRITES];
  const int count = decodeSprites(f.spriteRam, f.spriteBanks, list);
  const int scroll = bgScroll(f.vram[SCROLL_X_LO], f.vram[SCROLL_X_HI]);
  const uint8_t yscroll = f.vram[SCROLL_Y];

  for (int y = 0; y < VIS_LINES; y++) {
    drawLine(f, list, count, y);
    mixLine(f, y, scroll, yscroll);
  }
}

// Line buffer as the hardware: a pixel over an opaque one collides.
void Collision::drawLine(const Frame &f, const Sprite *list, int count, int y) {
  lineLo = LINE_X1 + 1;
  lineHi = LINE_X0 - 1;

  for (int n = 0; n < count; n++) {
    const Sprite &s = list[n];
    if (y < s.top || y >= s.bottom) {
      continue;
    }

    const uint8_t *bank = f.spriteRom + s.bank * SPRITE_BANK;
    walkRow(bank, rowAddr(s, y), [&](int i, uint8_t c) {
      if (c == 0) {
        return;
      }

      for (int x = s.x + 2 * i; x <= s.x + 2 * i + 1; x++) {
        if (x < LINE_X0 || x > LINE_X1) {
          continue;
        }

        if (line[x] != EMPTY) {
          spr[line[x] + SPRITES * n] = 1;
          sprSummary = 1;
        }
        line[x] = n;
        lineLo = x < lineLo ? x : lineLo;
        lineHi = x > lineHi ? x : lineHi;
      }
    });
  }
}

// Mixer PROM under each opaque sprite pixel; clears the line buffer behind it.
void Collision::mixLine(const Frame &f, int y, int scroll, uint8_t yscroll) {
  const uint8_t *bgPage = f.vram;
  const uint8_t *fgPage = f.vram + PAGE_BYTES;

  for (int x = lineLo; x <= lineHi; x++) {
    const int n = line[x];
    if (n == EMPTY) {
      continue;
    }
    line[x] = EMPTY;

    uint8_t fgColor, bgColor;
    const uint8_t fp = tilePixel(fgPage, f.tiles, f.tileMask, (uint8_t)(x / 2), y, fgColor);
    const uint8_t bp = tilePixel(bgPage, f.tiles, f.tileMask, bgCol(x, scroll),
                                 bgY(y, yscroll), bgColor);
    const uint8_t v = f.prom[mixIndex(true, fp, fgColor, bp, bgColor)];
    if (v & MIX_NO_COLLIDE) {
      continue;
    }

    mix[((v & MIX_BANK) << 2) | n] = 1;
    mixSummary = 1;
  }
}
