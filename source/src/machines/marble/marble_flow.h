#ifndef MARBLE_FLOW_H
#define MARBLE_FLOW_H

#include <stdint.h>
#include "marble_mem.h"

// Game flow of one player (MARBLE.md "Game flow"; model dev/marble/marble_flow.py): the
// 0x400390 sequencer of the arcade main loop 0x1101e and the race timer of the vblank IRQ
// 0x28a96. The arcade's blocking main loop parts (race intro, bonus wait, level load) are
// frame counters here, fitted to MAME traces.
//
//   newGame             RACE (0)                    RACE_END (3)
//   race 0 ---> intro: +5 s steps ---> timer, ticks ---> bonus: seconds * 100, level load
//     ^                                  | timer out       |            |
//     |                                  v                 | next race  | after ultimate
//   START -- ATTRACT (1) <-- GAME_OVER (2) <----           v            v
//     title: 3 marbles,        ^             TIME_OUT (4)    FINAL (6): 20000 + 1000 *
//     table, 600 frames        +------------------------- seconds - 1000 * deaths, 28 marbles
//     then a demo race
//
//   ATTRACT segments: title -> demo race (practice, beginner, practice, ...) -> title ...
//     demo: DEMO_LOAD (black) -> race init, DEMO_INTRO -> timer 60 s, DEMO_RUN (logic ticks,
//     the waypoint driver steers) -> waypoint list end, DEMO_END (HUD on) -> title
//
//   frame(f, m, start)  one 60 Hz frame: race timer, sequencer. Returns LOGIC (race on: the
//                       machine runs its logic tick when due), START_RACE (load race f.race:
//                       c = carry(f, m), cleared RAM, restore(m, c), startRace, then
//                       raceLoaded), or WAIT.
//   newGame(f)          practice race next frame (START in the title or after game over)
//   attract(f, m)       the title from the next frame on (boot, 7 frames after game over)
//   demoShown(f)        the attract demo race is on screen
//
// marble_physics::tick (0x251de) sets RACE_END once all players are out of play (goal).
namespace marble_flow {

static const uint32_t GAME_STATE = 0x400390;  // word

// 0x400390 values (0x1101e)
enum Phase : uint16_t {
  RACE = 0, ATTRACT = 1, GAME_OVER = 2, RACE_END = 3, TIME_OUT = 4, GAME_START = 5, FINAL = 6
};

enum Step : uint8_t { WAIT, LOGIC, START_RACE };

// attract demo race of the segment (MARBLE.md "Demo race"); NO_DEMO: the title
enum Demo : uint8_t { NO_DEMO, DEMO_LOAD, DEMO_INTRO, DEMO_RUN, DEMO_END };

// sequencer state, outside the work RAM image (a race load clears that)
struct Flow {
  Phase phase;     // last seen 0x400390
  uint16_t frame;  // frames since phase changed
  uint16_t intro;  // frames since the race init, NO_INTRO: no intro running
  uint8_t race;    // race to load / running
  bool fresh;      // new game: nothing carried
  int32_t final;   // final bonus points still to add
  uint8_t partMode, partFill;  // marble_particles init mode and palette fill of the marbles
  Demo demo;        // attract: demo part of the segment
  uint8_t demos;    // attract: demo races started (practice first, then beginner, ...)
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

// title (MARBLE.md "Attract"): marbles from ATTRACT frame TITLE_AT for TITLE_LEN frames
static const uint16_t TITLE_AT = 7, TITLE_LEN = 600;

void newGame(Flow &f);
void attract(Flow &f, marble_physics::Mem &m);
inline bool demoShown(const Flow &f) { return f.demo >= DEMO_INTRO; }
uint8_t raceTime(uint8_t race);       // intro: seconds races 0/1 set, later races add
uint16_t firstAdd(const Flow &f);     // intro frames from the race init to the first +5 s
Step frame(Flow &f, marble_physics::Mem &m, bool start);
Carry carry(const Flow &f, const marble_physics::Mem &m);
void restore(marble_physics::Mem &m, const Carry &c);
void raceLoaded(Flow &f, marble_physics::Mem &m);

// race timer 0x28a96 of player 1, one 60 Hz frame; true when it ran out
bool timer(marble_physics::Mem &m);
inline Phase phase(const marble_physics::Mem &m) { return (Phase)m.rw(GAME_STATE); }

}  // namespace marble_flow

#endif
