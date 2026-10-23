#include "marble_flow.h"
#include "marble_particles.h"
#include "marble_physics.h"
#include "marble_rng.h"
#include "marble_sound.h"

#include "../../catalog.h"
#ifdef GAME_MARBLE

// Port of dev/marble/marble_flow.py (Marble Love TS: game-tick-timers.ts, timer-cascade.ts,
// main-loop-init-1101e.ts, player-slot-iter-118d2.ts, state-sub-18a88.ts). Frame counts of
// the blocking main loop parts come from MAME frame traces (+-1 frame).

namespace marble_flow {

namespace {

using marble_physics::Mem;
using marble_physics::PLAYER;
using marble_physics::MSG_CHAIN;
using marble_physics::MSG_COUNT;
using marble_physics::MSG_FRAMES;
using marble_physics::MSG_STATE;

// player fields
enum : uint8_t {
  SLOT = 0x18, PLAYER_NO = 0x19, STATE = 0x1a,
  TIMER = 0x6a, TENTHS = 0x6c, TICKS = 0x6e, FLASH = 0x71,  // race timer 0x6a-0x6e
  FADE = 0x70, SCORE = 0xbc, DEATHS = 0xd2, SKIP = 0xd8,
};
static const uint8_t TIMER_BYTES = 6, STOPPED = 0xff, TICKS_START = 5, TENTHS_START = 9;
static const uint8_t OUT_OF_LIST = 2;           // +0x18 after the time out
static const uint8_t TIME_BONUS = 8;            // +0x1a: 0x28a96 skips the timer

static const uint32_t RNG = 0x4003a6, SCORED = 0x40039c;

// race intro: races 0/1 set their time, later races add it (+5 s steps)
static const uint8_t RACE_TIME[] = {60, 60, 35, 30, 20, 20};
static const uint8_t LAST_RACE = 5;
static const uint8_t ADD_STEP = 10, ADD_SECONDS = 5;
static const uint8_t INTRO_END = 61;            // last arm to race start: 61 + race time
static const uint8_t START_FIRST_ADD = 131;     // new game: init to the first +5 s (scroll in)
static const uint8_t FRAMES_TO_START = 6;       // 0x400010 vblank acks during the race end

// race end (offsets from the 0x400390 = 3 frame), per race just finished
static const uint8_t NEXT_INDEX = 2;            // race index + 1 written
static const uint8_t BONUS_WAIT = 40;           // 0x118d2 wait before the level load
static const uint8_t LOAD[] = {81, 80, 60, 116, 73};   // 0x16ec6 level load of the next race
static const uint8_t INIT_AFTER[] = {8, 8, 8, 9, 9};   // bonus to the next race init
static const uint8_t FIRST_ADD[] = {64, 63, 63, 64, 64};  // next race init to its first +5 s
static const uint8_t SECONDS_MAX = 99;          // bonus and final bonus clamp
static const uint8_t BONUS_POINTS = 100;        // per second left

// final bonus (0x18a88)
static const int32_t FINAL_BASE = 20000, FINAL_SECOND = 1000, FINAL_DEATH = 1000;
static const uint8_t DEATHS_MAX = 20;
static const uint8_t FINAL_AFTER = 6;           // bonus to state 6
static const uint8_t FINAL_FIRST = 10, FINAL_STEP_FRAMES = 2;
static const int32_t FINAL_STEP = 250;
static const uint16_t FINAL_END = 272;          // last step to game over (0x5a + 0xb4 waits)

// race music commands (MAME: 0x62, then this one about 60 frames into the intro)
static const uint8_t RACE_MUSIC[6] = {0x08, 0x0a, 0x0c, 0x0e, 0x14, 0x18};
static const uint16_t TIME_OUT_FRAMES = 182;    // 0x16a20 summary

// attract (MARBLE.md "Attract"): game over to the title, title segment
static const uint16_t GAME_OVER_ATTRACT = 7;    // 0x400390 = 2 to 1
static const uint16_t GAME_OVER_RNG = 0x100;    // one rng(0x100) at the game over
static const uint16_t TITLE_TAIL = 6;           // title end to the next segment
static const uint16_t FINAL_PARTS = 3;          // 0x400390 = 6 to the final screen marbles

// attract demo race (MARBLE.md "Demo race"), frames; index: demo race (practice, beginner)
static const uint8_t DEMO_RACES = 2;
static const uint8_t DEMO_LOAD_FRAMES[DEMO_RACES] = {59, 84};  // segment to the race init
static const uint8_t DEMO_START[DEMO_RACES] = {3, 4};  // race init to the first tick, timer
static const uint8_t DEMO_SECONDS = 60;
static const uint8_t DEMO_HUD_END = 5;          // waypoint list end to the title
static const uint16_t DEMO_TITLE_FRAME = 1;     // title frame at the demo end: marbles 6 later
static const uint32_t DEMO_FRAMES = 2;          // 0x400010 at the race init (first tick: 3)

// palette pulses (0x26bee score fade, 0x26c78 timer flash): counters wrap above these
static const int8_t FADE_WRAP = 0x3f, FLASH_WRAP = 0x1f;
static const uint8_t OFF = 0xff;
static const int16_t FLASH_ON = 4, FLASH_OFF = 5;  // 0x28a96: seconds when a second passes

int16_t seconds(const Mem &m) { return m.sw(PLAYER + TIMER); }

// 0x28608: points to the player score, its score changed flag
void addPoints(Mem &m, int32_t points) {
  m.wl(PLAYER + SCORE, m.rl(PLAYER + SCORE) + (uint32_t)points);
  const uint8_t bit = m.rb(PLAYER + PLAYER_NO) & 0x3f;
  if (bit < 8) { m.wb(SCORED, m.rb(SCORED) | (uint8_t)(1u << bit)); }
}

int16_t clamp(int16_t v, int16_t hi) { return v > hi ? hi : v; }

// 0x28c38 on the player timer: bit 0 seconds wrapped below 0, bit 1 a tenth passed
uint8_t cascade(Mem &m) {
  const uint32_t t = PLAYER + TIMER;
  if (m.rb(t + 4) == STOPPED) { return 0; }

  m.wb(t + 4, m.rb(t + 4) - 1);
  if (m.sb(t + 4) >= 0) { return 0; }
  m.wb(t + 4, TICKS_START);

  m.wb(t + 2, m.rb(t + 2) - 1);
  if (m.sb(t + 2) >= 0) { return 0; }
  m.wb(t + 2, TENTHS_START);

  m.ww(t, m.rw(t) - 1);
  return m.rw(t) == 0xffff ? 3 : 2;
}

// vblank anim ticks 0x26bee / 0x26c78 (before the race timer): step the pulse counters; the
// HUD picks the colours from them (marble_hud)
void pulses(Mem &m) {
  uint8_t c = m.rb(PLAYER + FADE);
  if (c != OFF && !m.rb(PLAYER + SKIP)) {
    c++;
    m.wb(PLAYER + FADE, (int8_t)c > FADE_WRAP ? 0 : c);
  }
  c = m.rb(PLAYER + FLASH);
  if (c != OFF) {
    c++;
    m.wb(PLAYER + FLASH, (int8_t)c > FLASH_WRAP ? 0 : c);
  }
}

// race intro of the loaded race: +5 s steps, then the timer starts and the race is on
void introStep(Flow &f, Mem &m) {
  const uint16_t first = firstAdd(f);
  const uint8_t time = RACE_TIME[f.race];
  const uint16_t i = ++f.intro;  // frames since the race init
  if (i >= first && (i - first) % ADD_STEP == 0 && (i - first) / ADD_STEP < time / ADD_SECONDS) {
    m.ww(PLAYER + TIMER, m.rw(PLAYER + TIMER) + ADD_SECONDS);
  }
  if (i != first - 1 + INTRO_END + time) { return; }

  m.wb(PLAYER + TICKS, TICKS_START);
  m.ww(GAME_STATE, RACE);
  f.intro = NO_INTRO;
  f.fresh = false;
}

// RACE_END: bonus, then the next race load or the final bonus (frames since 0x400390 = 3)
Step raceEnd(Flow &f, Mem &m) {
  const uint8_t race = f.race;
  const uint16_t bonus = NEXT_INDEX + BONUS_WAIT + (race < LAST_RACE ? LOAD[race] : 0);
  if (f.frame == NEXT_INDEX) {
    m.ww(marble_physics::RACE, race + 1);
    m.wb(PLAYER + SKIP, 0);  // 0x118d2: score fade on, timer flash off
    m.wb(PLAYER + FLASH, OFF);
    m.wb(PLAYER + FADE, 0);
  }
  if (f.frame == bonus) {
    m.wb(PLAYER + FADE, OFF);  // 0x118d2: score fade off with the points
    addPoints(m, clamp(seconds(m), SECONDS_MAX) * BONUS_POINTS);
  }

  if (race == LAST_RACE) {
    if (f.frame == bonus + FINAL_AFTER) {
      f.final = FINAL_BASE + clamp(seconds(m), SECONDS_MAX) * FINAL_SECOND -
                clamp((int16_t)m.rw(PLAYER + DEATHS), DEATHS_MAX) * FINAL_DEATH;
      m.ww(GAME_STATE, FINAL);
    }
    return WAIT;
  }
  if (f.frame != bonus + INIT_AFTER[race]) { return WAIT; }

  f.race = race + 1;
  return START_RACE;
}

// marbles: palette fill and init with the RNG of this frame
void startParts(Flow &f, Mem &m, uint8_t count, uint8_t mode) {
  f.partMode = mode;
  f.partFill = marble_particles::init(m, count, mode);
}

// game over: from FINAL and TIME_OUT; the final screen marbles go
void gameOver(Mem &m) {
  marble_particles::clear(m);
  m.ww(GAME_STATE, GAME_OVER);
  marble_sound::send(m, marble_sound::SND_GAME_OVER);
}

// FINAL: marbles, the final bonus in 250 point steps, then game over
void finalStep(Flow &f, Mem &m) {
  if (f.frame == FINAL_PARTS && !marble_particles::count(m)) {
    startParts(f, m, marble_particles::FINAL_COUNT, marble_particles::FINAL_MODE);
  }
  if (f.final > 0) {
    if (f.frame < FINAL_FIRST || (f.frame - FINAL_FIRST) % FINAL_STEP_FRAMES) { return; }
    f.final -= FINAL_STEP;
    addPoints(m, FINAL_STEP);
    if (f.final <= 0) { f.frame = 0; }  // count FINAL_END from the last step
    return;
  }
  if (f.frame == FINAL_END) { gameOver(m); }
}

// BIOS message slot 0 (vblank): a banner goes after its frames
void messageStep(Mem &m) {
  if (m.rb(MSG_STATE) != 1) { return; }

  const uint16_t n = m.rw(MSG_COUNT) + 1;
  m.ww(MSG_COUNT, n);
  if (n < m.rw(MSG_FRAMES)) { return; }
  m.wl(MSG_CHAIN, 0);
  m.wb(MSG_STATE, 0);
}

// demo race of the segment: level load, intro, logic ticks until the waypoint list ends
Step demoStep(Flow &f, Mem &m) {
  if (f.demo == DEMO_LOAD) { return f.frame == DEMO_LOAD_FRAMES[f.race] ? START_RACE : WAIT; }

  timer(m);
  messageStep(m);
  if (f.demo == DEMO_INTRO) {
    if (f.frame != DEMO_START[f.race]) { return WAIT; }
    m.ww(PLAYER + TIMER, DEMO_SECONDS);
    m.wb(PLAYER + TICKS, TICKS_START);
    f.demo = DEMO_RUN;
    return LOGIC;
  }
  if (f.demo == DEMO_RUN) {
    if (!marble_physics::demoEnded(m)) { return LOGIC; }
    f.demo = DEMO_END;
    f.frame = 1;
    return WAIT;
  }
  if (f.frame == DEMO_HUD_END) {  // DEMO_END: the title, its marbles where MAME shows them
    f.demo = NO_DEMO;
    f.frame = DEMO_TITLE_FRAME;
  }
  return WAIT;
}

// ATTRACT: title, then a demo race, until START
Step attractStep(Flow &f, Mem &m, bool start) {
  if (start) {
    newGame(f);
    return WAIT;
  }
  if (f.demo != NO_DEMO) { return demoStep(f, m); }

  if (f.frame == TITLE_AT) {
    startParts(f, m, marble_particles::TITLE_COUNT, marble_particles::TITLE_MODE);
  }
  if (f.frame == TITLE_AT + TITLE_LEN) { marble_particles::clear(m); }
  if (f.frame == TITLE_AT + TITLE_LEN + TITLE_TAIL) {  // next segment: a demo race
    f.demo = DEMO_LOAD;
    f.race = f.demos++ % DEMO_RACES;
    f.frame = 0;
  }
  return WAIT;
}

}  // namespace

void attract(Flow &f, Mem &m) {
  marble_particles::clear(m);
  m.ww(GAME_STATE, ATTRACT);
  f.phase = ATTRACT;
  f.frame = 0;
  f.intro = NO_INTRO;
  f.fresh = false;
  f.final = 0;
  f.demo = NO_DEMO;
  f.demos = 0;
}

void newGame(Flow &f) {
  f.phase = GAME_START;
  f.frame = 0;
  f.intro = NO_INTRO;
  f.race = 0;
  f.fresh = true;
  f.final = 0;
  f.demo = NO_DEMO;
}

uint8_t raceTime(uint8_t race) { return RACE_TIME[race <= LAST_RACE ? race : LAST_RACE]; }

uint16_t firstAdd(const Flow &f) { return f.fresh ? START_FIRST_ADD : FIRST_ADD[f.race - 1]; }

bool timer(Mem &m) {
  if (m.rb(PLAYER + STATE) == TIME_BONUS) { return false; }
  const uint8_t flags = cascade(m);
  if (flags & 2) {  // a second passed: timer flash from 4 s on, off above 5 s
    if (seconds(m) == FLASH_ON) { m.wb(PLAYER + FLASH, 0); }
    if (seconds(m) > FLASH_OFF) { m.wb(PLAYER + FLASH, OFF); }
  }
  if (!(flags & 1)) { return false; }

  // 0x28a96: out of play, timer cleared and stopped
  m.wb(PLAYER + SLOT, OUT_OF_LIST);
  m.ww(PLAYER + TIMER, 0);
  m.wb(PLAYER + TENTHS, 0);
  m.wb(PLAYER + TICKS, STOPPED);
  m.wb(PLAYER + FLASH, 0xff);
  m.ww(GAME_STATE, TIME_OUT);
  marble_sound::send(m, marble_sound::SND_TIME_OUT);
  marble_sound::send(m, marble_sound::SND_TIME_OUT_JINGLE);
  return true;
}

Step frame(Flow &f, Mem &m, bool start) {
  if (f.fresh && f.intro == NO_INTRO) { return START_RACE; }  // new game: load practice

  // a phase set last frame (logic tick, timer, sequencer) counts from 1 here
  if (phase(m) != f.phase) {
    f.phase = phase(m);
    f.frame = 0;
  }
  f.frame++;
  if (marble_particles::count(m)) { marble_particles::bounce(m); }  // inits come below
  if (f.phase == ATTRACT) { return attractStep(f, m, start); }
  pulses(m);
  timer(m);
  if (f.intro != NO_INTRO) { introStep(f, m); }
  if (phase(m) != f.phase) { return WAIT; }  // time out or race start: from next frame on

  switch (f.phase) {
    case RACE: return f.intro == NO_INTRO ? LOGIC : WAIT;
    case RACE_END: return f.intro == NO_INTRO ? raceEnd(f, m) : WAIT;
    case FINAL: finalStep(f, m); return WAIT;
    case TIME_OUT:
      if (f.frame == TIME_OUT_FRAMES) { gameOver(m); }
      return WAIT;
    case GAME_OVER:
      if (f.frame == 1) { marble_rng::rng(m, GAME_OVER_RNG); }
      if (start) {
        newGame(f);
      } else if (f.frame == GAME_OVER_ATTRACT) {
        attract(f, m);
      }
      return WAIT;
    default: return WAIT;
  }
}

Carry carry(const Flow &f, const Mem &m) {
  Carry c = {};
  c.rng = m.rw(RNG);
  c.frames = m.rl(marble_physics::FRAMES);
  if (f.fresh || f.demo != NO_DEMO) { return c; }

  for (uint8_t k = 0; k < TIMER_BYTES; k++) { c.timer[k] = m.rb(PLAYER + TIMER + k); }
  c.score = m.rl(PLAYER + SCORE);
  c.deaths = m.rw(PLAYER + DEATHS);
  return c;
}

void restore(Mem &m, const Carry &c) {
  m.ww(RNG, c.rng);
  m.wl(marble_physics::FRAMES, c.frames + FRAMES_TO_START);
  for (uint8_t k = 0; k < TIMER_BYTES; k++) { m.wb(PLAYER + TIMER + k, c.timer[k]); }
  m.wl(PLAYER + SCORE, c.score);
  m.ww(PLAYER + DEATHS, c.deaths);
}

// race init (0x10504 player part): the timer waits for the intro; races 0/1 start from 0 s.
// Attract demo: 0x400390 stays 1, the waypoint driver steers, no intro, no sound
void raceLoaded(Flow &f, Mem &m) {
  if (f.race < 2) { m.ww(PLAYER + TIMER, 0); }
  m.wb(PLAYER + TENTHS, TENTHS_START);
  m.wb(PLAYER + TICKS, STOPPED);
  if (f.demo == DEMO_LOAD) {
    m.ww(GAME_STATE, ATTRACT);
    m.wl(marble_physics::FRAMES, DEMO_FRAMES);
    marble_physics::demoStart(m, f.race);
    f.demo = DEMO_INTRO;
    f.frame = 0;
    return;
  }
  m.ww(GAME_STATE, f.fresh ? GAME_START : RACE_END);
  f.phase = phase(m);
  f.intro = 0;
  marble_sound::send(m, marble_sound::SND_STOP);
  marble_sound::send(m, RACE_MUSIC[f.race]);
}

}  // namespace marble_flow

#endif  // GAME_MARBLE
