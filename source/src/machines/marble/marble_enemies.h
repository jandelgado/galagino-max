#ifndef MARBLE_ENEMIES_H
#define MARBLE_ENEMIES_H

#include <stdint.h>
#include "marble_mem.h"

// Enemies (MARBLE.md "Enemies"), a 1:1 port of tools/marble_enemy.py on the work RAM image.
// Phase 4b-1: steelies (black marbles), 2 records of 0x7c bytes at 0x4009a4, the player
// object layout plus AI fields. They spawn by camera band, follow a node graph (patrol),
// hunt or chase the player when angry, bump the marble and break or shatter on landings.
//
//   init(m)                     race start: steelie numbers +0x19 (set at boot in the arcade)
//   cameraBands(m, old, new)    band step of 0x144e4: steelie spawns/removals (0x15a12)
//   update(m)                   per logic tick after the player: both steelies (0x158cc)
//   pair_1bc88(m, a2)           object a2 against the player and steelies, from the physics;
//                               true if it collided (the caller refreshes its cell)
//   land_25c74(m, a2, fall)     non-player landing: stunned, shatters on a high fall
//   remove_15bd0(m, a2, s, c)   steelie scored (s) and/or freed with its draw list entry (c)
namespace marble_enemies {

static const uint32_t STEELIES = 0x4009a4;
static const uint16_t STEELIE_SIZE = 0x7c;
static const uint8_t NSTEELIES = 2;

enum Score : uint8_t { NO_SCORE, SCORE };
enum Clear : uint8_t { KEEP, CLEAR };

void init(marble_physics::Mem &m);
void cameraBands(marble_physics::Mem &m, int16_t oldBand, int16_t newBand);
void update(marble_physics::Mem &m);
bool pair_1bc88(marble_physics::Mem &m, uint32_t a2);
void land_25c74(marble_physics::Mem &m, uint32_t a2, int16_t fall);
void remove_15bd0(marble_physics::Mem &m, uint32_t a2, Score s, Clear c);

}  // namespace marble_enemies

#endif
