#ifndef WILLIAMS_VIDEO_H
#define WILLIAMS_VIDEO_H

#include <stdint.h>
#include <string.h>

// 292 native columns to <= 240 panel columns: drop `dropA`/`dropB` of every
// `period` columns; a dropped pixel folds into its left neighbour
// (out = a ? a : b), so 1 px shots and stars stay visible.
struct ColumnLayout {
  uint16_t skip;    // native columns cropped on the left
  uint16_t width;   // native columns used
  uint8_t period;
  uint8_t dropA;
  uint8_t dropB;    // NO_DROP if only one column per period is dropped
};

static const uint8_t NO_DROP = 0xff;
// full 292 px, 2 of 11 dropped: 239 columns
static const ColumnLayout DEFENDER_COLUMNS = { 0, 292, 11, 3, 8 };
// arena walls at x 6..283, 1 of 7 dropped: 239
static const ColumnLayout ROBOTRON_COLUMNS = { 6, 278, 7, 5, NO_DROP };
// Joust uses all 292 columns (attract probe 2026-10-14)
static const ColumnLayout JOUST_COLUMNS = { 0, 292, 11, 3, 8 };

class ColumnMap {
public:
  static const int OUT_MAX = 240;

  void build(const ColumnLayout &l) {
    n = 0;
    for (int x = 0; x < l.width && n < OUT_MAX; x++) {
      const int k = x % l.period;
      if (k == l.dropA || k == l.dropB) {
        if (n > 0) {
          mergeNext[n - 1] = 1;
        }
        continue;
      }
      source[n] = l.skip + x;
      mergeNext[n] = 0;
      n++;
    }
    leftPad = (OUT_MAX - n) / 2;
  }

  uint8_t count() const { return n; }
  uint8_t left() const { return leftPad; }
  uint16_t src(int i) const { return source[i]; }
  bool merge(int i) const { return mergeNext[i]; }

private:
  uint16_t source[OUT_MAX];
  uint8_t mergeNext[OUT_MAX];
  uint8_t n = 0, leftPad = 0;
};

// ROT270 upright (Sinistar): panel row r shows native column 289 - r
// (292 columns, 2 cropped per end), panel column c shows scanout row c
static const int UPRIGHT_CROP = 2, NATIVE_COLUMNS = 292;
static inline uint16_t uprightX(int row) { return NATIVE_COLUMNS - 1 - UPRIGHT_CROP - row; }

// pixel x of a scanout row (146 bytes, 2 px each, even x = high nibble)
static inline uint8_t williamsPixel(const uint8_t *row, uint16_t x) {
  const uint8_t b = row[x >> 1];
  return (x & 1) ? (b & 0x0f) : (b >> 4);
}

// What the CRT shows: VRAM lines copied as the emulated beam passes them,
// into the back buffer; flip() at the end of the frame publishes it. The
// video core latches front() once per frame in prepare_frame().
//
//   emulation core                    video core
//   run_frame: capture() x 65  -> back
//              flip()          -> front ---> prepare_frame: shown = front()
//                                            render_row: frame(shown)
class Scanout {
public:
  static const int ROWS = 240;        // visible lines 7..246
  static const int COLS = 146;        // VRAM bytes per visible line (292 px)
  static const int FIRST_LINE = 7;
  static const int LINES = 260;
  static const int BAND_LINES = 4;    // one capture() per CPU slice
  static const int BANDS = LINES / BAND_LINES;
  static const int BUF_BYTES = ROWS * COLS;

  void attach(uint8_t *a, uint8_t *b) {
    buf[0] = a;
    buf[1] = b;
  }

  void clear() {
    memset(buf[0], 0, BUF_BYTES);
    memset(buf[1], 0, BUF_BYTES);
    memset(pal, 0, sizeof(pal));
  }

  void capture(const uint8_t *vram, uint8_t firstCol, int line, int count,
               const uint8_t *palette16) {
    const uint8_t back = frontIndex ^ 1;
    memcpy(pal[back][line / BAND_LINES], palette16, PENS);
    const uint8_t *band = vram + firstCol * VRAM_STRIDE + line;
    const int top = line - FIRST_LINE;
    if (count == BAND_LINES && top >= 0 && top + BAND_LINES <= ROWS &&
        !((uintptr_t)band & (BAND_LINES - 1))) {
      captureBand(band, buf[back] + top * COLS);
      return;
    }

    for (int l = line; l < line + count; l++) {
      const int row = l - FIRST_LINE;
      if (row < 0 || row >= ROWS) {
        continue;
      }
      uint8_t *dst = buf[back] + row * COLS;
      const uint8_t *src = vram + firstCol * VRAM_STRIDE + l;
      for (int c = 0; c < COLS; c++) {
        dst[c] = src[c * VRAM_STRIDE];
      }
    }
  }

  void flip() {
    __sync_synchronize();  // buffer writes visible before the index
    frontIndex ^= 1;
  }

  uint8_t front() const { return frontIndex; }
  const uint8_t *frame(uint8_t index) const { return buf[index]; }
  const uint8_t *palette(uint8_t index, int line) const { return pal[index][line / BAND_LINES]; }

private:
  static const int PENS = 16;
  static const int VRAM_STRIDE = 256;  // byte (x / 2) * 256 + y

  // A band's 4 lines of one VRAM column are adjacent: one word load per
  // column instead of 4 byte loads 256 apart. Lowest byte = first line.
  static void captureBand(const uint8_t *src, uint8_t *dst) {
    for (int c = 0; c < COLS; c++, src += VRAM_STRIDE) {
      uint32_t w;
      memcpy(&w, __builtin_assume_aligned(src, 4), sizeof(w));
      dst[c] = (uint8_t)w;
      dst[c + COLS] = (uint8_t)(w >> 8);
      dst[c + 2 * COLS] = (uint8_t)(w >> 16);
      dst[c + 3 * COLS] = (uint8_t)(w >> 24);
    }
  }

  uint8_t *buf[2] = { nullptr, nullptr };
  uint8_t pal[2][BANDS][PENS];
  volatile uint8_t frontIndex = 0;
};

#endif
