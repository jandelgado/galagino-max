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
static const uint16_t SPRITE_TILES = 531;  // intermediate sprite set, 32 B per tile; romconv checks

struct Level {
  uint16_t rows;
  uint16_t tiles;
  const uint16_t *map;          // rows * COLS
  const uint8_t (*pixels)[64];  // per tile 8x8 colour indices 0..31
  const uint16_t *palette;      // 4 sets x 32, RGB565 byte-swapped
  // level object sprite tiles of the race's set (4 bpp, left pixel in the high nibble);
  // null: none
  const uint8_t (*sprites)[32];
};

inline uint16_t tileOf(uint16_t w) { return w & 0x3ff; }
inline uint8_t setOf(uint16_t w) { return (w >> 10) & 3; }
inline bool flipOf(uint16_t w) { return (w & 0x8000) != 0; }

static const uint8_t NO_PATCH = 0xff;

// index into marble_patches of the animation frame record, NO_PATCH if there is none
uint8_t patchIndex(uint32_t record);

// Arena buffers for the largest level, allocated once; load() unpacks into them
// (85 KB: tiles 57344 B in one Arena block, map 10440 B, sprite tiles 16992 B).
class Store {
public:
  void init();  // idempotent: start() runs twice
  const Level &load(uint8_t n);
  // Playfield patch `index` of marble_patches (mode 1 level object, MARBLE.md "Level
  // objects"): write its block into the map, clipped to the map. Ignored when it belongs
  // to another race or the index is out of range (NO_PATCH).
  void patch(uint8_t index);

private:
  uint8_t race = 0xff;
  uint16_t *map = nullptr;
  uint8_t (*pixels)[64] = nullptr;
  uint8_t (*sprites)[32] = nullptr;
  uint8_t sprSet = 0xff;  // sprite set decoded into `sprites`
  Level level = {};
};

}  // namespace marble_level

#endif
