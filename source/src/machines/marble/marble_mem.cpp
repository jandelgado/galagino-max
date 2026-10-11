#include "marble_mem.h"

#ifdef ENABLE_MARBLE

#include <stdio.h>
#include <stdlib.h>
#include "../../emulation/asset.h"
#include "marble_rom.h"

namespace marble_physics {

static const uint8_t RACES = 6;
static const uint8_t PAIR_BYTES = 54;  // 44 fields of 10 bits per diagonal pair
static const uint8_t FIELDS = 44;      // field 43 (column 21, odd diagonal) not stored

void fail(const char *what, uint32_t addr) {
  printf("marble physics: %s access at %06x\n", what, (unsigned)addr);
  abort();
}

void Mem::setRace(uint8_t race) {
  if (race >= RACES) { fail("race", race); }
  terrain = &marble_terrain[race];
}

uint8_t Mem::romb(uint32_t a) const {
  // blocks are sorted by address and do not overlap: last block starting at or below a
  uint8_t lo = 0, hi = MARBLE_PROG_BLOCKS;
  while (lo < hi) {
    const uint8_t mid = (lo + hi) / 2;
    if (marble_prog_blocks[mid].addr <= a) { lo = mid + 1; } else { hi = mid; }
  }
  if (lo) {
    const MarbleProgBlock &b = marble_prog_blocks[lo - 1];
    if (a - b.addr < b.len) { return b.data[a - b.addr]; }
  }
  fail("ROM", a);
}

uint16_t Mem::maxd() const { return terrain->maxd; }

uint32_t Mem::desc() const { return terrain->desc; }

int16_t Mem::edgeFrom(uint8_t col, uint8_t d) const {
  const uint16_t ult = rw(ULT_COL);
  const uint8_t frame = ult / 0x90;
  if (col >= 36 || ult % 0x90 || frame >= terrain->nedges) { fail("terrain edges", ult); }

  // ascending list ending in maxd - 1: the scan stops there for d < maxd
  const uint8_t *p = marble_edge_lists + terrain->edges[36 * frame + col];
  while (*p < d) { p++; }
  return *p;
}

uint16_t Mem::base(int16_t d) const {
  if (d < 0 || d >= terrain->maxd) { fail("terrain base", (uint16_t)d); }
  return terrain->bases[d];
}

// 10 bit fields MSB first, field f at bit 10 * f of the pair (0x1a9cc layout).
uint16_t Mem::vertexCode(int16_t d, int16_t f) const {
  if (d < 0 || d >= terrain->maxd) { fail("terrain diagonal", (uint16_t)d); }
  if (f < 0 || f >= FIELDS) { fail("terrain field", (uint16_t)f); }

  uint16_t idx = 0;
  if (f < FIELDS - 1) {
    const uint8_t *p = terrain->index + PAIR_BYTES * (d >> 1) + 10 * f / 8;
    idx = ((p[0] << 8 | p[1]) >> (6 - 10 * f % 8)) & 0x3ff;
  }
  if (idx >= terrain->ncodes) { fail("terrain code index", idx); }
  return terrain->codes[idx];
}

uint8_t Mem::record(uint16_t code, uint8_t q) const {
  const uint32_t i = (uint32_t)code + q;
  if (i >= terrain->nrecords) { fail("terrain record", i); }
  return terrain->records[i];
}

}  // namespace marble_physics

#endif  // ENABLE_MARBLE
