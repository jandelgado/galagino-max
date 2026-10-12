#ifndef MARBLE_ENEMIES_H
#define MARBLE_ENEMIES_H

#include <stdint.h>
#include "marble_mem.h"

// Enemies (MARBLE.md "Enemies"), a 1:1 port of tools/marble_enemy.py on the work RAM image.
// Phase 4b-1: steelies (black marbles), 2 records of 0x7c bytes at 0x4009a4, the player
// object layout plus AI fields. They spawn by camera band, follow a node graph (patrol),
// hunt or chase the player when angry, bump the marble and break or shatter on landings.
// Phase 4b-2: slinkies (green worms, marble_slinkies.cpp), 4 records of 0x60 bytes at
// 0x401302. They walk waypoint lists, get pushed by the marble, chase and eat it.
// Phase 4b-3: acid (green puddles, marble_acid.cpp), 7 records of 0x42 bytes at 0x401482.
// They crawl their paths and dissolve the marble.
//
//   init(m)                     race start: steelie, slinky and acid numbers +0x19 (set at
//                               boot in the arcade)
//   cameraBands(m, old, new)    band step of 0x144e4: steelie spawns/removals (0x15a12), then
//                               slinkies (0x14c46), then acid (0x17346)
//   update(m)                   per logic tick after the player: both steelies (0x158cc), then
//                               the slinkies (0x1493c), then the acid (0x17230)
//   contact_14e92(m, a2)        player a2 against the slinkies, from the player's physics
//   contact_175c8(m, a2)        player a2 against the acid, right after; true if it dissolves
//                               (the caller refreshes its cell)
//   target_176d2(m, a2)         dissolving player a2 slides into its acid
//   pair_1bc88(m, a2)           object a2 against the player and steelies, from the physics;
//                               true if it collided (the caller refreshes its cell)
//   land_25c74(m, a2, fall)     non-player landing: stunned, shatters on a high fall
//   remove_15bd0(m, a2, s, c)   steelie scored (s) and/or freed with its draw list entry (c)
namespace marble_enemies {

static const uint32_t STEELIES = 0x4009a4;
static const uint16_t STEELIE_SIZE = 0x7c;
static const uint8_t NSTEELIES = 2;
static const uint32_t SLINKIES = 0x401302;
static const uint16_t SLINKY_SIZE = 0x60;
static const uint8_t NSLINKIES = 4;
static const uint32_t ACIDS = 0x401482;
static const uint16_t ACID_SIZE = 0x42;
static const uint8_t NACIDS = 7;

enum Score : uint8_t { NO_SCORE, SCORE };
enum Clear : uint8_t { KEEP, CLEAR };

void init(marble_physics::Mem &m);
void cameraBands(marble_physics::Mem &m, int16_t oldBand, int16_t newBand);
void update(marble_physics::Mem &m);
bool pair_1bc88(marble_physics::Mem &m, uint32_t a2);
void contact_14e92(marble_physics::Mem &m, uint32_t a2);
bool contact_175c8(marble_physics::Mem &m, uint32_t a2);
void target_176d2(marble_physics::Mem &m, uint32_t a2);
void land_25c74(marble_physics::Mem &m, uint32_t a2, int16_t fall);
void remove_15bd0(marble_physics::Mem &m, uint32_t a2, Score s, Clear c);

}  // namespace marble_enemies

#endif
