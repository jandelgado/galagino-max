#ifndef MARBLE_LEVEL_H
#define MARBLE_LEVEL_H

#include <stdint.h>

// One race's playfield: 36 tiles (288 px) wide, rows x 8 px high, its own tile set
// (MARBLE.md "Levels"). Map word: bits 0-9 tile, 10-11 colour set (32 colours each),
// 15 horizontal flip.
namespace marble_level {

static const uint8_t LEVELS = 6;
static const uint8_t COLS = 36;
static const uint16_t WIDTH = COLS * 8;  // 288
static const uint16_t MAX_ROWS = 145;    // beginner race; romconv checks
static const uint16_t MAX_TILES = 896;   // beginner race; romconv checks

struct Level {
  uint16_t rows;
  uint16_t tiles;
  const uint16_t *map;          // rows * COLS
  const uint8_t (*pixels)[64];  // per tile 8x8 colour indices 0..31
  const uint16_t *palette;      // 4 sets x 32, RGB565 byte-swapped
};

inline uint16_t tileOf(uint16_t w) { return w & 0x3ff; }
inline uint8_t setOf(uint16_t w) { return (w >> 10) & 3; }
inline bool flipOf(uint16_t w) { return (w & 0x8000) != 0; }

// Arena buffers for the largest level, allocated once; load() unpacks into them
// (68 KB: tiles 57344 B in one Arena block, map 10440 B).
class Store {
public:
  void init();  // idempotent: start() runs twice
  const Level &load(uint8_t n);
  // Playfield patch of the loaded race (mode 1 level object, MARBLE.md "Level objects"):
  // write the block of frame record `record` into the map, clipped to the map. Unknown
  // records are ignored.
  void patch(uint32_t record);

private:
  uint8_t race = 0xff;
  uint16_t *map = nullptr;
  uint8_t (*pixels)[64] = nullptr;
  Level level = {};
};

}  // namespace marble_level

#endif
