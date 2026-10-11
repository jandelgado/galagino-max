#include "marble_level.h"

#ifdef ENABLE_MARBLE

#include "../../emulation/arena.h"
#include "../../emulation/asset.h"
#include "marble_rom.h"

namespace marble_level {

void Store::init() {
  if (!map) { map = Arena::alloc<uint16_t>(MAX_ROWS * COLS); }
  if (!pixels) { pixels = (uint8_t (*)[64])Arena::alloc(MAX_TILES * 64, 4); }
}

const Level &Store::load(uint8_t n) {
  const Asset<unsigned short, COMPRESSED> m(marble_maps[n]);
  m.decodeInto(map);
  const Asset<unsigned char[64], COMPRESSED> t(marble_tiles[n]);
  t.decodeInto(pixels);
  level = {marble_levels[n].rows, marble_levels[n].tiles, map, pixels, marble_pf_palette[n]};
  return level;
}

}  // namespace marble_level

#endif  // ENABLE_MARBLE
