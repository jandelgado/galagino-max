#include "marble_level.h"

#include "../../catalog.h"
#ifdef GAME_MARBLE

#include "../../emulation/arena.h"
#include "../../emulation/asset.h"
#include "marble_rom.h"

static_assert(MARBLE_SPRITE_MAX_TILES == marble_level::SPRITE_TILES, "sprite tile buffer");
static_assert(MARBLE_TITLE_LEVEL == marble_level::TITLE, "title level index");

namespace marble_level {

void Store::init() {
  if (!map) { map = Arena::alloc<uint16_t>(MAX_ROWS * COLS); }
  if (!pixels) { pixels = (uint8_t (*)[64])Arena::alloc(MAX_TILES * 64, 4); }
  if (!sprites) { sprites = (uint8_t (*)[32])Arena::alloc(SPRITE_TILES * 32, 4); }
}

const Level &Store::load(uint8_t n) {
  const Asset<unsigned short, COMPRESSED> m(marble_maps[n]);
  m.decodeInto(map);
  const Asset<unsigned char[64], COMPRESSED> t(marble_tiles[n]);
  t.decodeInto(pixels);
  if (sprSet == NO_SET) {  // first load: the death tiles of every race open the buffer
    const Asset<unsigned char[32], COMPRESSED> d(marble_death_tiles_blob);
    d.decodeInto(sprites);
  }
  const uint8_t set = n < LEVELS ? marble_sprite_race[n] : sprSet;  // races share sets
  if (set != sprSet) {
    const Asset<unsigned char[32], COMPRESSED> s(marble_sprite_tiles[set]);
    s.decodeInto(sprites + MARBLE_DEATH_TILES);
    sprSet = set;
  }
  race = n;
  level = {marble_levels[n].rows, marble_levels[n].tiles, map, pixels, marble_pf_palette()[n],
           sprites};
  return level;
}

void Store::patch(uint8_t index) {
  if (index >= MARBLE_PATCHES) { return; }
  const MarblePatch &p = marble_patches[index];
  if (p.race != race) { return; }
  for (uint16_t y = 0; y < p.h; y++) {
    if (p.row + y >= level.rows) { break; }
    for (uint16_t x = 0; x < p.w && p.col + x < COLS; x++) {
      map[(p.row + y) * COLS + p.col + x] = p.words[y * p.w + x];
    }
  }
}

uint8_t patchIndex(uint32_t record) {
  for (uint8_t i = 0; i < MARBLE_PATCHES; i++) {
    if (marble_patches[i].record == record) { return i; }
  }
  return NO_PATCH;
}

}  // namespace marble_level

#endif  // GAME_MARBLE
