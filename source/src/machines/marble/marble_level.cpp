#include "marble_level.h"

#ifdef ENABLE_MARBLE

#include "../../emulation/arena.h"
#include "../../emulation/asset.h"
#include "marble_rom.h"

namespace marble_level {

static const uint8_t PATCH_RACE = 0;  // practice: the start gate

void Store::init() {
  if (!map) { map = Arena::alloc<uint16_t>(MAX_ROWS * COLS); }
  if (!pixels) { pixels = (uint8_t (*)[64])Arena::alloc(MAX_TILES * 64, 4); }
}

const Level &Store::load(uint8_t n) {
  const Asset<unsigned short, COMPRESSED> m(marble_maps[n]);
  m.decodeInto(map);
  const Asset<unsigned char[64], COMPRESSED> t(marble_tiles[n]);
  t.decodeInto(pixels);
  race = n;
  level = {marble_levels[n].rows, marble_levels[n].tiles, map, pixels, marble_pf_palette[n]};
  return level;
}

void Store::patch(uint32_t record) {
  if (race != PATCH_RACE) { return; }
  for (const MarblePatch &p : marble_patches) {
    if (p.record != record) { continue; }
    for (uint16_t y = 0; y < p.h; y++) {
      if (p.row + y >= level.rows) { break; }
      for (uint16_t x = 0; x < p.w && p.col + x < COLS; x++) {
        map[(p.row + y) * COLS + p.col + x] = p.words[y * p.w + x];
      }
    }
    return;
  }
}

}  // namespace marble_level

#endif  // ENABLE_MARBLE
