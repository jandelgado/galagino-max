#include "marble_occlusion.h"

#ifdef ENABLE_MARBLE

#include "marble_physics.h"

// Integer semantics of the 68010 as in marble_physics.cpp: words wrap at 16 bits, >> on
// signed values is asr.

namespace marble_occlusion {

using marble_physics::Mem;

namespace {

// object fields
enum : uint8_t {
  PX = 0x0c, PY = 0x10, PZ = 0x14,
  STATE = 0x1a,
  VISIBLE = 0x1c,
  SX = 0x1e, SY = 0x20,
  MASKS = 0x38,
  SURFACE = 0x58, SURFACE_T = 0x59,
};
enum : uint8_t { ST_DIZZY = 1, ST_BROKEN = 2, ST_LOST = 4, ST_GOAL = 6, ST_5 = 5, ST_9 = 9,
                 ST_A = 0xa, ST_B = 0xb };

static const uint32_t PLAYER2 = 0x4000fa;
static const uint32_t CAMERA = 0x40097e;
static const uint32_t FLAG = 0x400988;      // long: quadrant parity of the vertex pair
static const uint32_t FLAG_COL = 0x40098a;  // word: column bias of the vertex field
static const uint32_t ULT_COL = 0x40045c;
static const uint32_t DYN = 0x40076e;
static const uint32_t RACE = 0x400394;

static const uint32_t SHAPES = 0x1eaba;     // 3 x 5 diagonal offsets per column
static const uint32_t BITS = 0x1eaca;       // 8 masks, bit i = diagonals >= i
static const uint32_t OCCLUDERS = 0x1ea2a;  // per column: table of per race record lists
static const uint32_t FLAT_H = 0x1ead2;     // entry code by height difference
static const uint32_t FLAT_Y = 0x1eaf4;
static const uint32_t DROP = 0x1ed62;
static const uint32_t QUADS = 0x2417e;      // 4 x (2 longs: quadrant offsets)
static const uint32_t QUAD_MASKS = 0x24176;

// descriptor
static const uint32_t DESC_TOP = 0x10, DESC_END = 0x18, DESC_EDGES = 0x2e;

static const uint16_t MASK_FULL = 0x6ce0;  // height 6 tiles, MO code 0x4e0 (all pen 1)
static const uint8_t COLS = 5;
static const int16_t FULL_SHIFT = 0x2f;

inline int8_t s8(uint32_t v) { return (int8_t)v; }
inline int16_t s16(uint32_t v) { return (int16_t)v; }

bool isPlayer(uint32_t a) { return a == marble_physics::PLAYER || a == PLAYER2; }

// 0x177f8: height (word) of the terrain vertex on diagonal d under screen column col, the
// higher of two quadrants picked by k + FLAG. 0 = no floor. The sim reads the terrain
// video RAM image; here the field comes from the terrain index (Mem::vertexCode).
uint16_t vertexHeight_177f8(const Mem &m, int32_t d, int32_t col, int16_t k) {
  const uint32_t desc = m.desc();
  if (s16(d) >= s16(m.romw(desc + DESC_END))) { return 0; }

  const int32_t d3 = k + m.sl(FLAG);
  int32_t f = (s16(col + m.rw(FLAG_COL)) >> 1) + 2;
  if (d & 1) { f += 22; }

  uint16_t code = m.vertexCode((int16_t)d, (int16_t)f);
  const uint16_t base = m.base((int16_t)d);

  for (;;) {
    if (code & 0xf000) {
      if (code & 0xf000 & m.romw(QUAD_MASKS + s16(d3 * 2))) {
        return (uint16_t)((code & 0x7f) - 0x40 + base);
      }

      const uint16_t bias = m.romw(DROP + ((code & 0xf80) >> 6));
      if (bias == 0x1000) { return 0; }
      return (uint16_t)((code & 0x7f) - 0x40 - s16(bias) + base);
    }
    if ((code & 0xfff) == 0) { return 0; }
    if (code & 0x800) {
      code = m.rw(DYN + (code & 0x7fe));
      continue;
    }

    const uint32_t tab = QUADS + 8 * (d3 & 0x1fffffff);
    const uint8_t a = m.record(code, (uint8_t)m.roml(tab));
    const uint8_t b = m.record(code, (uint8_t)m.roml(tab + 4));
    const uint8_t hv = a > b ? a : b;
    if (hv == 0) { return 0; }
    return (uint16_t)(hv - 0x80 + base);
  }
}

// 0x264aa: up to 5 entries (code, x, y) at +0x38, one per 8 px screen column under the
// sprite. Entry code: bits 12-15 MO height in tiles, bit 15 also hflip (span masks), bit
// 11 set, bits 0-10 MO code. The MO builder turns them into priority MOs (pen 1 =
// playfield shows). +0x1c = 0 when every column is covered. d3: 0 player, 1 other.
void occlusion_264aa(Mem &m, uint32_t a2, uint8_t d3) {
  uint32_t out = a2 + MASKS;
  auto emit = [&](uint16_t code, uint16_t x, uint16_t y) {
    m.ww(out, code);
    m.ww(out + 2, x);
    m.ww(out + 4, y);
    out += 6;
  };

  for (uint8_t i = 0; i < COLS; i++) { m.ww(a2 + MASKS + 6 * i, 0); }

  const uint8_t st = m.rb(a2 + STATE);
  if (d3 == 0) {
    const uint8_t sf = m.rb(a2 + SURFACE);
    if (sf && m.rb(a2 + SURFACE_T) != 0xff && sf != ST_A && st != ST_9 && st != ST_B &&
        st != ST_A) {
      m.wb(a2 + VISIBLE, 0);
      return;
    }
    if (st == ST_LOST || st == ST_GOAL || st == ST_A) { return; }
  }

  const int32_t x = m.sl(a2 + PX), y = m.sl(a2 + PY);
  const int16_t sy = m.sw(a2 + SY);
  const int32_t col = m.sw(a2 + SX) >> 3;

  int32_t ncol = 3, top = 0xe;
  if (d3 == 0 && (st == ST_DIZZY || st == ST_BROKEN || st == ST_5 || st == ST_9)) {
    ncol = 5;
    top = 0x20;
  }
  top = s16(top + sy);  // sprite top line (counting up)

  const int32_t diag = s16((y >> 19) + (x >> 19) - 0x15);
  const int32_t diag2 = s16((int32_t)(((int64_t)x + y) >> 19) - 0x15);
  uint32_t shape = SHAPES;
  if (diag != diag2) {
    shape += 10;
  } else if ((col ^ diag) & 1) {
    shape += 5;
  }

  int32_t c0;
  if (ncol == 3) {
    shape += 1;
    c0 = col - 1;
  } else {
    c0 = col - 2;
  }
  if (c0 < 0) {
    ncol += c0;
    shape -= c0;
    c0 = 0;
  } else if (ncol + c0 > 0x24) {
    ncol = 0x24 - c0;
  }

  const uint32_t desc = m.desc();
  const int32_t cam = s16(m.rw(CAMERA) - m.romw(desc + DESC_TOP));
  const uint16_t race = m.rw(RACE);
  uint32_t covered = 0;

  auto full = [&](int32_t c) {
    covered |= 1u << (c - c0);
    emit(MASK_FULL, (uint16_t)(c << 3), (uint16_t)(top - FULL_SHIFT));
  };

  for (int32_t c = c0; c < c0 + ncol; c++) {
    int32_t d = s8(m.romb(shape + c - c0)) + diag;
    if (d < 0) { d = 0; }

    // explicit occluders per column and race: records of 5 words (diagonal limit, depth,
    // control, y, entry code)
    const uint32_t tab = m.roml(OCCLUDERS + 4 * c);
    const uint16_t sel = m.romw(tab + 2 * race);
    bool done = false;
    if (sel) {
      uint32_t r = tab + 2 * sel;
      for (;;) {
        const int32_t lim = s16(m.romw(r));
        if (lim == -1) { break; }
        if (lim < d) {
          r += 10;
          continue;
        }
        if (d < lim - s16(m.romw(r + 2))) { break; }

        const int32_t ctl = s16(m.romw(r + 4));
        const int32_t yy = s16(m.romw(r + 6) + cam);
        const uint16_t code = m.romw(r + 8);
        done = true;
        if (ctl >= 0 && ctl < 8) {
          if (yy >= sy - ctl) {
            full(c);
          } else {
            emit(code, (uint16_t)(c << 3), (uint16_t)yy);
          }
        } else if (ctl == 8) {
          if (yy < top) { emit(code, (uint16_t)(c << 3), (uint16_t)yy); }
        } else if (ctl > 0) {
          if (yy <= top) {
            emit(code, (uint16_t)(c << 3), (uint16_t)yy);
          } else {
            d = ctl & 0xff;
            done = false;
          }
        } else if (ctl == -2) {
          if (s16(code) > (m.sl(a2 + PZ) >> 16)) { full(c); }
        } else if (!(d == lim && diag2 == lim)) {
          emit(code, (uint16_t)(c << 3), (uint16_t)yy);
        }
        break;
      }
    }
    if (done) { continue; }

    // first terrain edge diagonal >= d in this column (bitmap, descriptor +0x2e)
    const uint32_t bm = m.roml(desc + DESC_EDGES + 4 * c + s16(m.rw(ULT_COL))) + (d >> 3);
    uint8_t mask = m.romb(BITS + (d & 7)) & m.romb(bm);
    d &= ~7;
    if (mask == 0) {
      uint32_t p = bm + 1;
      while (m.romb(p) == 0) { p++; }
      mask = m.romb(p);
      d += (p - bm) * 8;
    }
    for (uint8_t b = 1; !(mask & b); b <<= 1) { d++; }

    m.wl(FLAG, (c ^ d) & 1);
    int32_t sline = s16((d << 2) - m.rw(CAMERA));
    int32_t y0 = s16(vertexHeight_177f8(m, d, c, 0) - sline);
    if (y0 > top + 0x10) {
      full(c);
      continue;
    }
    if (y0 < sy - 0x10) { continue; }

    m.wl(FLAG, m.rl(FLAG) ^ 1);
    sline = s16(sline + 4);
    int32_t y1 = s16(vertexHeight_177f8(m, d + 1, c, 2) - sline);
    if (y1 <= sy && y0 <= sy) { continue; }
    if (y1 >= top && y0 >= top) {
      full(c);
      continue;
    }
    if (m.rl(FLAG) == 0) {
      const int32_t t = y0;
      y0 = y1;
      y1 = t;
    }

    if (y0 <= y1) {
      const uint32_t i = 2 * (y1 - y0);
      emit(m.romw(FLAT_H + i), (uint16_t)(c << 3), (uint16_t)(m.romw(FLAT_Y + i) + y0));
    } else {
      const uint32_t i = 2 * (y0 - y1);
      emit((uint16_t)(m.romw(FLAT_H + i) + 0x8000), (uint16_t)(c << 3),
           (uint16_t)(m.romw(FLAT_Y + i) + y1));
    }
  }

  const uint32_t all = (1u << ncol) - 1;
  if ((covered & all) == all || (d3 == 0 && ncol == COLS && (covered & 0xe) == 0xe)) {
    m.wb(a2 + VISIBLE, 0);
  }
}

}  // namespace

// 0x1281c: +0x1c = on screen (-16 < sy < 256); visible objects get their occlusion masks
void enter(Mem &m, uint32_t obj) {
  m.wb(obj + VISIBLE, 0);
  const int16_t sy = m.sw(obj + SY);
  if (sy <= -16 || sy >= 0x100) { return; }

  m.wb(obj + VISIBLE, 1);
  occlusion_264aa(m, obj, isPlayer(obj) ? 0 : 1);
}

}  // namespace marble_occlusion

#endif  // ENABLE_MARBLE
