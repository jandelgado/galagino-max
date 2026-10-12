#ifndef MARBLE_FLOW_H
#define MARBLE_FLOW_H

#include <stdint.h>
#include "marble_mem.h"

// Game flow of one player (MARBLE.md "Game flow"; model tools/marble_flow.py): the
// 0x400390 sequencer of the arcade main loop 0x1101e and the race timer of the vblank IRQ
// 0x28a96. The arcade's blocking main loop parts (race intro, bonus wait, level load) are
// frame counters here, fitted to MAME traces.
//
//   newGame             RACE (0)                    RACE_END (3)
//   race 0 ---> intro: +5 s steps ---> timer, ticks ---> bonus: seconds * 100, level load
//                 ^                      | timer out       |            |
//                 |                      v                 | next race  | after ultimate
//                 +---- START ---- GAME_OVER (2) <----     v            v
//                                        ^       TIME_OUT (4)    FINAL (6): 20000 + 1000 *
//                                        +------------------------ seconds - 1000 * deaths
//
//   frame(f, m, start)  one 60 Hz frame: race timer, sequencer. Returns LOGIC (race on: the
//                       machine runs its logic tick when due), START_RACE (load race f.race:
//                       c = carry(f, m), cleared RAM, restore(m, c), startRace, then
//                       raceLoaded), or WAIT.
//   newGame(f)          practice race next frame (boot, START after game over)
//
// marble_physics::tick (0x251de) sets RACE_END once all players are out of play (goal).
namespace marble_flow {

static const uint32_t GAME_STATE = 0x400390;  // word

// 0x400390 values (0x1101e); the remake waits for START in GAME_OVER (no attract mode yet)
enum Phase : uint16_t {
  RACE = 0, ATTRACT = 1, GAME_OVER = 2, RACE_END = 3, TIME_OUT = 4, GAME_START = 5, FINAL = 6
};

enum Step : uint8_t { WAIT, LOGIC, START_RACE };

// sequencer state, outside the work RAM image (a race load clears that)
struct Flow {
  Phase phase;     // last seen 0x400390
  uint16_t frame;  // frames since phase changed
  uint16_t intro;  // frames since the race init, NO_INTRO: no intro running
  uint8_t race;    // race to load / running
  bool fresh;      // new game: nothing carried
  int32_t final;   // final bonus points still to add
};
static const uint16_t NO_INTRO = 0xffff;

// what a race load keeps (0x10504 does not clear it)
struct Carry {
  uint8_t timer[6];  // player +0x6a-+0x6f
  uint32_t score;    // +0xbc
  uint16_t deaths;   // +0xd2
  uint16_t rng;      // 0x4003a6
  uint32_t frames;   // 0x400010
};

void newGame(Flow &f);
Step frame(Flow &f, marble_physics::Mem &m, bool start);
Carry carry(const Flow &f, const marble_physics::Mem &m);
void restore(marble_physics::Mem &m, const Carry &c);
void raceLoaded(Flow &f, marble_physics::Mem &m);

// race timer 0x28a96 of player 1, one 60 Hz frame; true when it ran out
bool timer(marble_physics::Mem &m);
inline Phase phase(const marble_physics::Mem &m) { return (Phase)m.rw(GAME_STATE); }

}  // namespace marble_flow

#endif
